//
// Copyright (C) OpenSim Ltd.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program; if not, see http://www.gnu.org/licenses/.
//

package inet.common.queueing;

import inet.common.queueing.base.PacketSourceBase;
import inet.common.queueing.contract.IPacketProvider;

//
// This module is a passive packet source which can be popped for packets from
// the connected packet collector.
//
simple PacketProvider extends PacketSourceBase like IPacketProvider
{
    parameters:
        volatile double providingInterval @unit(s) = default(0s); // elapsed time between subsequent packets allowed to be popped by the connected packet collector, 0 means any number of packets can be popped at the same simulation time
        @class(PacketProvider);
        @signal[packetPopped](type=inet::Packet);
        @signal[packetCreated](type=inet::Packet);
        @statistic[packetCreated](title="created packets"; record=count,sum(packetBytes),vector(packetBytes); interpolationmode=none);
    gates:
        output out @labels(pop);
}
