# SPDX-License-Identifier: AGPL-3.0-or-later
# protocol_roundtrip: encode and decode control-surface packets with the fetched firmware's
# CPktMgr and DeviceControlContracts ids. Exit status 0 when every packet survives the trip.
add_executable(protocol_roundtrip main.cpp)
kickem_link_protocol(protocol_roundtrip)
