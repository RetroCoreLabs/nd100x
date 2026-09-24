/*
 * mfbus_config.c - build the multifunction bus from a parsed .ini
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * This file sees nd100x's headers ONLY. It maps a parsed configuration onto the
 * mfbus_bridge API and never touches an nd500x type - see mfbus_config.h for the
 * include-guard collision that makes the separation necessary rather than
 * stylistic.
 */

#ifdef ND100X_WITH_ND500

#include "mfbus_config.h"

#include "mfbus_bridge.h"

/* devices_types.h BEFORE devices_protos.h: the generated protos name Device and
 * DeviceClass without declaring them. */
#include "../devices/devices_types.h"
#include "../devices/devices_protos.h"
#include "../devices/octobus/device_octobus.h"
#include "../ndlib/ndlib_types.h"
#include "../ndlib/ndlib_protos.h"

bool mfbus_apply_config(const MachineConfig *mc)
{
    if (mc == NULL || !mc->mfbus.enabled)
    {
        if (mc != NULL && mc->nd5000Count > 0)
        {
            /* mc_validate() refuses this, so reaching it means a configuration
             * was applied without being validated. Say so rather than building
             * CPUs that cannot run. */
            LOG(LOG_CAT_MMS, LOG_WARN,
                "ND-5000 CPUs are configured but there is no [mfbus] pool - none were added\n");
        }
        return false;
    }

    /* Defaults that match the schema's documented ones rather than zero: a pool
     * of no size and a base page of 0 would both be accepted here and then fail
     * deep inside the bank table. */
    uint32_t size_mb = (mc->mfbus.size_mb > 0) ? (uint32_t)mc->mfbus.size_mb : 16u;
    uint32_t base_page = mc->mfbus.base_page_set ? (uint32_t)mc->mfbus.base_page : 04100u;

    if (!mfbus_attach(size_mb * 1024u * 1024u, base_page))
    {
        return false;
    }

    /* The ND-100's way onto the bus. Station 1B is fixed in the hardware, so the
     * card carries no station setting - only which of the four interfaces it is.
     * Found by its IOX address because the device manager indexes by address. */
    if (mc->octobus.enabled)
    {
        devmgr_add_device(DEVICE_TYPE_OCTOBUS, 0);
        Device *card = devmgr_get_device_by_address(OCTOBUS_BASE_ADDRESS);
        if (card != NULL)
        {
            (void)mfbus_attach_card(card);
        }
        else
        {
            LOG(LOG_CAT_MMS, LOG_WARN,
                "MFbus: the octobus card was added but could not be found to connect "
                "to the bus\n");
        }
    }

    for (int i = 0; i < mc->nd5000Count; i++)
    {
        const McNd5000 *cpu5 = &mc->nd5000[i];
        if (!cpu5->enabled)
        {
            continue;
        }

        /* A per-CPU failure is logged and skipped: one bad station must not cost
         * the others, and the reason is already in the log. */
        if (!mfbus_add_nd5000((uint8_t)cpu5->station))
        {
            continue;
        }
        if (!mfbus_attach_cpu((uint8_t)cpu5->station))
        {
            continue;
        }

        /*
         * A kernel named in the .ini is loaded at pool offset 0 ONLY because no
         * mailbox has been placed yet. Loading over the mailbox global header
         * would overwrite X5SEM with program text, and the symptom is a
         * semaphore that never unlocks.
         *
         * mc_validate() has already refused a configuration where more than one
         * enabled CPU names boot material, so at most one CPU reaches this.
         */
        if (cpu5->kernel[0] != '\0')
        {
            (void)mfbus_load_nd5000((uint8_t)cpu5->station, cpu5->kernel, 0);
        }
    }

    return true;
}

#endif /* ND100X_WITH_ND500 */
