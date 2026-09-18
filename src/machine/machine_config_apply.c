/*
 * machine_config_apply.c - turn a MachineConfig into an actual machine.
 *
 * MOVED here from src/frontend/nd100x/nd100x.c, where it was a static function
 * called apply_machine_config(). See the header for why. The body is unchanged
 * apart from the two CLI override flags, which are now parameters instead of
 * reads of the native frontend's global `config` - that was the only thing
 * tying it to a command line.
 */
#include "machine_config_apply.h"
#include "machine_protos.h"
#include "../cpu/cpu_types.h"
#include "../devices/devices_types.h"
#include "../devices/devices_protos.h"

void MachineConfig_ApplyCpu(const MachineConfig *mc, const MachineConfigApplyOpts *opts)
{
    MachineConfigApplyOpts none = {0, 0};
    int ct;

    if (!mc) return;
    if (!opts) opts = &none;

    /* cpu_model is the resolved one: it carries a model name like ND110CX that
     * the family number cannot express. Falling back to the number keeps a
     * config built by hand (or by an older writer) working. */
    if (mc->cpu_model != 0)
        g_current_cpu_type = (CpuType)mc->cpu_model;
    else if (MachineConfig_CpuTypeForNumber(mc->cpu_type, &ct))
        g_current_cpu_type = (CpuType)ct;

    // FPP width from the .ini [machine] fpp= key; a --fpp CLI flag wins
    // (mirroring the --memory / memory= precedence rule).
    if (!opts->fpp_already_set)
        g_current_fpp_type = (mc->fpp_bits == 32) ? FPP32 : FPP48;

    // RTC time base from the .ini [machine] rtc= key: ticks (default, one pulse
    // per 10550 instructions) or wall (one pulse per 20 ms of host time).
    // A --rtc CLI flag wins (same precedence rule as --fpp).
    if (!opts->rtc_already_set)
        RTC_SetWallClockMode(mc->rtc_wall);

}

void MachineConfig_ApplyDevices(const MachineConfig *mc)
{
    if (!mc) return;

    for (int i = 0; i < mc->terminalCount; i++)
        DeviceManager_AddDevice(DEVICE_TYPE_TERMINAL, (uint8_t)mc->terminals[i]);

    for (int i = 0; i < mc->controllerCount; i++) {
        const MC_Controller *c = &mc->controllers[i];
        if (!c->enabled) continue;

        if (c->type == CTRL_SMD) {
            for (int s = 0; s < 4 && s < MC_MAX_DISK_SLOTS; s++)
                if (c->disks[s].present) mount_smd(c->disks[s].image, s);
        } else if (c->type == CTRL_FLOPPY) {
            for (int s = 0; s < 3 && s < MC_MAX_DISK_SLOTS; s++)
                if (c->disks[s].present) mount_floppy(c->disks[s].image, s);
        } else if (c->type == CTRL_WINCHESTER) {
            /* Opt-in card at IOX 500-507 (same block as the CDC system disc);
             * not added by DeviceManager_AddAllDevices, so add it here. */
            DeviceManager_AddDevice(DEVICE_TYPE_DISC_WINCHESTER, (uint8_t)c->wheel);
            for (int s = 0; s < 2 && s < MC_MAX_DISK_SLOTS; s++)
                if (c->disks[s].present) mount_winchester(c->disks[s].image, s);
        } else if (c->type == CTRL_SCSI) {
            SCSIUnitType types[SCSI_MAX_UNITS];
            for (int s = 0; s < SCSI_MAX_UNITS; s++) types[s] = SCSI_UNIT_NONE;
            for (int s = 0; s < SCSI_MAX_UNITS; s++) {
                if (c->disks[s].present) {
                    types[s] = c->disks[s].media;
                    mount_scsi(c->disks[s].image, s);
                }
            }
            DeviceManager_AddSCSIDevice_WithConfig(c->wheel, types);
        } else if (c->type == CTRL_HDLC) {
            machine_add_hdlc(c->wheel, c->hdlc_is_server,
                             c->hdlc_host[0] ? c->hdlc_host : NULL, c->hdlc_port);
        }
    }
}
