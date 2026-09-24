/*
 * machine_config_apply.c - turn a MachineConfig into an actual machine.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
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
#include "mfbus_bridge.h"

void mc_apply_cpu(const MachineConfig *mc, const MachineConfigApplyOpts *opts)
{
    MachineConfigApplyOpts none = {0, 0};
    int ct;

    if (!mc)
    {
        return;
    }
    if (!opts)
    {
        opts = &none;
    }

    /* cpu_model is the resolved one: it carries a model name like ND110CX that
     * the family number cannot express. Falling back to the number keeps a
     * config built by hand (or by an older writer) working. */
    if (mc->cpu_model != 0)
    {
        g_current_cpu_type = (CpuType)mc->cpu_model;
    }
    else if (mc_cpu_type_for_number(mc->cpu_type, &ct))
    {
        g_current_cpu_type = (CpuType)ct;
    }

    // FPP width from the .ini [machine] fpp= key; a --fpp CLI flag wins
    // (mirroring the --memory / memory= precedence rule).
    if (!opts->fpp_already_set)
    {
        g_current_fpp_type = (mc->fpp_bits == 32) ? FPP32 : FPP48;
    }

    // RTC time base from the .ini [machine] rtc= key: ticks (default, one pulse
    // per 10550 instructions) or wall (one pulse per 20 ms of host time).
    // A --rtc CLI flag wins (same precedence rule as --fpp).
    if (!opts->rtc_already_set)
    {
        rtc_set_wall_clock_mode(mc->rtc_wall);
    }
}

void mc_apply_devices(const MachineConfig *mc)
{
    if (!mc)
    {
        return;
    }

    for (int i = 0; i < mc->terminalCount; i++)
    {
        devmgr_add_device(DEVICE_TYPE_TERMINAL, (uint8_t)mc->terminals[i]);
    }

    for (int i = 0; i < mc->controllerCount; i++)
    {
        const McController *c = &mc->controllers[i];
        if (!c->enabled)
        {
            continue;
        }

        if (c->type == CTRL_SMD)
        {
            for (int s = 0; s < 4 && s < MC_MAX_DISK_SLOTS; s++)
            {
                if (c->disks[s].present)
                {
                    machine_mount_smd(c->disks[s].image, s);
                }
            }
        }
        else if (c->type == CTRL_FLOPPY)
        {
            for (int s = 0; s < 3 && s < MC_MAX_DISK_SLOTS; s++)
            {
                if (c->disks[s].present)
                {
                    machine_mount_floppy(c->disks[s].image, s);
                }
            }
        }
        else if (c->type == CTRL_WINCHESTER)
        {
            /* Opt-in card at IOX 500-507 (same block as the CDC system disc);
             * not added by DeviceManager_AddAllDevices, so add it here. */
            devmgr_add_device(DEVICE_TYPE_DISC_WINCHESTER, (uint8_t)c->wheel);
            for (int s = 0; s < 2 && s < MC_MAX_DISK_SLOTS; s++)
            {
                if (c->disks[s].present)
                {
                    machine_mount_winchester(c->disks[s].image, s);
                }
            }
        }
        else if (c->type == CTRL_SCSI)
        {
            SCSIUnitType types[SCSI_MAX_UNITS];
            for (int s = 0; s < SCSI_MAX_UNITS; s++)
            {
                types[s] = SCSI_UNIT_NONE;
            }
            for (int s = 0; s < SCSI_MAX_UNITS; s++)
            {
                if (c->disks[s].present)
                {
                    types[s] = c->disks[s].media;
                    machine_mount_scsi(c->disks[s].image, s);
                }
            }
            devmgr_add_scsi_device_with_config(c->wheel, types);
        }
        else if (c->type == CTRL_HDLC)
        {
            machine_add_hdlc(c->wheel, c->hdlc_is_server, c->hdlc_host[0] ? c->hdlc_host : NULL,
                             c->hdlc_port);
        }
    }

#ifdef ND100X_WITH_ND500
    /*
     * The multifunction bus: ONE shared memory pool, the ND-100's octobus card,
     * and a station per ND-5000.
     *
     * ORDER MATTERS. The pool is attached first because both of the others
     * depend on it - a station with no shared memory has nowhere to execute -
     * and it is registered as an ND_MEM_MPM5 bank, which is what makes SINTRAN
     * find it at all.
     */
    if (mc->mfbus.enabled)
    {
        /* Defaults that match the schema's documented ones rather than zero: a
         * pool of no size and a base page of 0 would both be accepted here and
         * then fail deep inside the bank table. */
        uint32_t size_mb = (mc->mfbus.size_mb > 0) ? (uint32_t)mc->mfbus.size_mb : 16u;
        uint32_t base_page = mc->mfbus.base_page_set ? (uint32_t)mc->mfbus.base_page : 04100u;

        if (mfbus_attach(size_mb * 1024u * 1024u, base_page))
        {
            /* The ND-100's way onto the bus. Station 1B is fixed in the
             * hardware, so the card carries no station setting - only which of
             * the four interfaces it is. */
            if (mc->octobus.enabled)
            {
                devmgr_add_device(DEVICE_TYPE_OCTOBUS, 0);
            }

            for (int i = 0; i < mc->nd5000Count; i++)
            {
                const McNd5000 *cpu5 = &mc->nd5000[i];
                if (!cpu5->enabled)
                {
                    continue;
                }
                (void)mfbus_add_nd5000((uint8_t)cpu5->station);
            }
        }
    }
    else if (mc->nd5000Count > 0)
    {
        /* mc_validate() refuses this, so reaching it means the configuration was
         * applied without being validated. Say so rather than building a
         * machine with CPUs that cannot run. */
        LOG(LOG_CAT_MMS, LOG_WARN,
            "ND-5000 CPUs are configured but there is no [mfbus] pool - none were added\n");
    }
#endif /* ND100X_WITH_ND500 */
}
