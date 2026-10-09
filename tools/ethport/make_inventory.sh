#!/bin/sh
# Regenerate docs/ethernet-port/cs-inventory.csv from the RetroCore sources
# that are in scope for the Ethernet II port (plan section 1).
# Reads the pinned snapshot made by tools/ethport/snapshot_retrocore.sh
# (build/ethport-cs-snapshot), never RetroCore's working tree. Needs the .NET SDK.
set -e
cd "$(dirname "$0")/../.."
SNAP=build/ethport-cs-snapshot
[ -f "$SNAP/COMMIT" ] || { echo "run tools/ethport/snapshot_retrocore.sh first" >&2; exit 1; }
R="$SNAP/Emulated.HW"
export MSBUILDDISABLENODEREUSE=1 DOTNET_CLI_TELEMETRY_OPTOUT=1
dotnet build tools/ethport/cs_inventory -c Release -nologo -v q -p:UseSharedCompilation=false >/dev/null
dotnet tools/ethport/cs_inventory/bin/Release/net10.0/cs_inventory.dll "$SNAP" \
    "$R/ND/CPU/NDBUS/NDBusEthernetII.cs" \
    "$R/ND/CPU/NDBUS/NDBusEthernetIIDecode.cs" \
    "$R/AMD/LANCE/Am7990/Am2990Lance.cs" \
    "$R/AMD/LANCE/Am7990/Enums.cs" \
    "$R/Motorola/MFP/MC68901/MC68901MFP.cs" \
    "$R/Motorola/MFP/MC68901/MfpTimer.cs" \
    "$R/Motorola/MFP/MC68901/Registers.cs" \
    "$R/Motorola/MFP/MC68901/Usart.cs" \
    "$R/Motorola/MFP/MC68901/HelperEnum.cs" \
    "$R/Common/Network/IEthernetBackend.cs" \
    "$R/Common/Network/EthernetBackendFactory.cs" \
    "$R/Common/Network/NullEthernetBackend.cs" \
    "$R/Common/Network/UdpEthernetBackend.cs" \
    "$R/Common/Network/TcpEthernetBackend.cs" \
    "$R/Common/Network/TcpEthernetRelay.cs" \
    "$R/Common/Network/PcapEthernetBackend.cs" \
    "$R/Common/Network/InProcessEthernetBridge.cs" \
    "$R/Common/Network/IpChecksumRepair.cs" \
    > docs/ethernet-port/cs-inventory.csv
echo "wrote docs/ethernet-port/cs-inventory.csv ($(($(wc -l < docs/ethernet-port/cs-inventory.csv) - 1)) rows)"
