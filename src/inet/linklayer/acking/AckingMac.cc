//
// Copyright (C) 2013 OpenSim Ltd
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
// along with this program; if not, see <http://www.gnu.org/licenses/>.
//
// author: Zoltan Bojthe
//

#include <stdio.h>
#include <string.h>

#include "inet/common/INETUtils.h"
#include "inet/common/ModuleAccess.h"
#include "inet/common/ProtocolGroup.h"
#include "inet/common/ProtocolTag_m.h"
#include "inet/common/packet/Packet.h"
#include "inet/linklayer/acking/AckingMac.h"
#include "inet/linklayer/acking/AckingMacHeader_m.h"
#include "inet/linklayer/common/InterfaceTag_m.h"
#include "inet/linklayer/common/MacAddressTag_m.h"
#include "inet/networklayer/contract/IInterfaceTable.h"

namespace inet {

using namespace inet::physicallayer;

Define_Module(AckingMac);

AckingMac::AckingMac()
{
}

AckingMac::~AckingMac()
{
    delete lastSentPk;
    cancelAndDelete(ackTimeoutMsg);
}

void AckingMac::flushQueue()
{
    ASSERT(queue);
    while (!queue->isEmpty()) {
        auto packet = queue->popPacket();
        PacketDropDetails details;
        details.setReason(INTERFACE_DOWN);
        emit(packetDroppedSignal, packet, &details);
        delete packet;
    }
}

void AckingMac::clearQueue()
{
    ASSERT(queue);
    while (!queue->isEmpty())
        delete queue->popPacket();
}

void AckingMac::initialize(int stage)
{
    MacProtocolBase::initialize(stage);
    if (stage == INITSTAGE_LOCAL) {
        bitrate = par("bitrate");
        headerLength = par("headerLength");
        promiscuous = par("promiscuous");
        fullDuplex = par("fullDuplex");
        useAck = par("useAck");
        ackTimeout = par("ackTimeout");

        cModule *radioModule = gate("lowerLayerOut")->getPathEndGate()->getOwnerModule();
        radioModule->subscribe(IRadio::transmissionStateChangedSignal, this);
        radio = check_and_cast<IRadio *>(radioModule);
        transmissionState = IRadio::TRANSMISSION_STATE_UNDEFINED;

        queue = check_and_cast<queueing::IPacketQueue *>(getSubmodule("queue"));
    }
    else if (stage == INITSTAGE_LINK_LAYER) {
        radio->setRadioMode(fullDuplex ? IRadio::RADIO_MODE_TRANSCEIVER : IRadio::RADIO_MODE_RECEIVER);
        if (useAck)
            ackTimeoutMsg = new cMessage("link-break");
        if (!queue->isEmpty())
            startTransmitting(queue->popPacket());
    }
}

void AckingMac::configureInterfaceEntry()
{
    MacAddress address = parseMacAddressParameter(par("address"));

    // data rate
    interfaceEntry->setDatarate(bitrate);

    // generate a link-layer address to be used as interface token for IPv6
    interfaceEntry->setMacAddress(address);
    interfaceEntry->setInterfaceToken(address.formInterfaceIdentifier());

    // MTU: typical values are 576 (Internet de facto), 1500 (Ethernet-friendly),
    // 4000 (on some point-to-point links), 4470 (Cisco routers default, FDDI compatible)
    interfaceEntry->setMtu(par("mtu"));

    // capabilities
    interfaceEntry->setMulticast(true);
    interfaceEntry->setBroadcast(true);
}

void AckingMac::receiveSignal(cComponent *source, simsignal_t signalID, long value, cObject *details)
{
    Enter_Method_Silent();
    if (signalID == IRadio::transmissionStateChangedSignal) {
        IRadio::TransmissionState newRadioTransmissionState = static_cast<IRadio::TransmissionState>(value);
        if (transmissionState == IRadio::TRANSMISSION_STATE_TRANSMITTING && newRadioTransmissionState == IRadio::TRANSMISSION_STATE_IDLE) {
            radio->setRadioMode(fullDuplex ? IRadio::RADIO_MODE_TRANSCEIVER : IRadio::RADIO_MODE_RECEIVER);
            if (!lastSentPk && !queue->isEmpty())
                startTransmitting(queue->popPacket());
        }
        transmissionState = newRadioTransmissionState;
    }
}

void AckingMac::startTransmitting(Packet *msg)
{
    // if there's any control info, remove it; then encapsulate the packet
    if (lastSentPk)
        throw cRuntimeError("Model error: unacked send");
    MacAddress dest = msg->getTag<MacAddressReq>()->getDestAddress();
    encapsulate(check_and_cast<Packet *>(msg));

    if (!dest.isBroadcast() && !dest.isMulticast() && !dest.isUnspecified()) {    // unicast
        if (useAck) {
            lastSentPk = msg->dup();
            scheduleAt(simTime() + ackTimeout, ackTimeoutMsg);
        }
    }

    // send
    EV << "Starting transmission of " << msg << endl;
    radio->setRadioMode(fullDuplex ? IRadio::RADIO_MODE_TRANSCEIVER : IRadio::RADIO_MODE_TRANSMITTER);
    sendDown(msg);
}

void AckingMac::handleUpperPacket(Packet *packet)
{
    EV << "Received " << packet << " for transmission\n";
    queue->pushPacket(packet);
    if (lastSentPk || radio->getTransmissionState() == IRadio::TRANSMISSION_STATE_TRANSMITTING)
        EV << "Delaying transmission of " << packet << ".\n";
    else
        startTransmitting(queue->popPacket());
}

void AckingMac::handleLowerPacket(Packet *packet)
{
    auto macHeader = packet->peekAtFront<AckingMacHeader>();
    if (packet->hasBitError()) {
        EV << "Received frame '" << packet->getName() << "' contains bit errors or collision, dropping it\n";
        PacketDropDetails details;
        details.setReason(INCORRECTLY_RECEIVED);
        emit(packetDroppedSignal, packet, &details);
        delete packet;
        return;
    }

    if (!dropFrameNotForUs(packet)) {
        int senderModuleId = macHeader->getSrcModuleId();
        AckingMac *senderMac = dynamic_cast<AckingMac *>(getSimulation()->getModule(senderModuleId));
        // TODO: this whole out of bounds ack mechanism is fishy
        if (senderMac && senderMac->useAck)
            senderMac->acked(packet);
        // decapsulate and attach control info
        decapsulate(packet);
        EV << "Passing up contained packet '" << packet->getName() << "' to higher layer\n";
        sendUp(packet);
    }
}

void AckingMac::handleSelfMessage(cMessage *message)
{
    if (message == ackTimeoutMsg) {
        EV_DETAIL << "AckingMac: timeout: " << lastSentPk->getFullName() << " is lost\n";
        auto macHeader = lastSentPk->popAtFront<AckingMacHeader>();
        lastSentPk->addTagIfAbsent<PacketProtocolTag>()->setProtocol(ProtocolGroup::ethertype.getProtocol(macHeader->getNetworkProtocol()));
        // packet lost
        emit(linkBrokenSignal, lastSentPk);
        delete lastSentPk;
        lastSentPk = nullptr;
        if (!queue->isEmpty())
            startTransmitting(queue->popPacket());
    }
    else {
        MacProtocolBase::handleSelfMessage(message);
    }
}

void AckingMac::acked(Packet *frame)
{
    Enter_Method_Silent();
    ASSERT(useAck);

    EV_DEBUG << "AckingMac::acked(" << frame->getFullName() << ") is ";

    if (lastSentPk && lastSentPk->getTreeId() == frame->getTreeId()) {
        EV_DEBUG << "accepted\n";
        cancelEvent(ackTimeoutMsg);
        delete lastSentPk;
        lastSentPk = nullptr;
        if (!queue->isEmpty())
            startTransmitting(queue->popPacket());
    }
    else
        EV_DEBUG << "unaccepted\n";
}

void AckingMac::encapsulate(Packet *packet)
{
    auto macHeader = makeShared<AckingMacHeader>();
    macHeader->setChunkLength(B(headerLength));
    auto macAddressReq = packet->getTag<MacAddressReq>();
    macHeader->setSrc(macAddressReq->getSrcAddress());
    macHeader->setDest(macAddressReq->getDestAddress());
    MacAddress dest = macAddressReq->getDestAddress();
    if (dest.isBroadcast() || dest.isMulticast() || dest.isUnspecified())
        macHeader->setSrcModuleId(-1);
    else
        macHeader->setSrcModuleId(getId());
    macHeader->setNetworkProtocol(ProtocolGroup::ethertype.getProtocolNumber(packet->getTag<PacketProtocolTag>()->getProtocol()));
    packet->insertAtFront(macHeader);
    auto macAddressInd = packet->addTagIfAbsent<MacAddressInd>();
    macAddressInd->setSrcAddress(macHeader->getSrc());
    macAddressInd->setDestAddress(macHeader->getDest());
    packet->getTag<PacketProtocolTag>()->setProtocol(&Protocol::ackingMac);
}

bool AckingMac::dropFrameNotForUs(Packet *packet)
{
    auto macHeader = packet->peekAtFront<AckingMacHeader>();
    // Current implementation does not support the configuration of multicast
    // MAC address groups. We rather accept all multicast frames (just like they were
    // broadcasts) and pass it up to the higher layer where they will be dropped
    // if not needed.
    // All frames must be passed to the upper layer if the interface is
    // in promiscuous mode.

    if (macHeader->getDest().equals(interfaceEntry->getMacAddress()))
        return false;

    if (macHeader->getDest().isBroadcast())
        return false;

    if (promiscuous || macHeader->getDest().isMulticast())
        return false;

    EV << "Frame '" << packet->getName() << "' not destined to us, discarding\n";
    PacketDropDetails details;
    details.setReason(NOT_ADDRESSED_TO_US);
    emit(packetDroppedSignal, packet, &details);
    delete packet;
    return true;
}

void AckingMac::decapsulate(Packet *packet)
{
    const auto& macHeader = packet->popAtFront<AckingMacHeader>();
    auto macAddressInd = packet->addTagIfAbsent<MacAddressInd>();
    macAddressInd->setSrcAddress(macHeader->getSrc());
    macAddressInd->setDestAddress(macHeader->getDest());
    packet->addTagIfAbsent<InterfaceInd>()->setInterfaceId(interfaceEntry->getInterfaceId());
    auto payloadProtocol = ProtocolGroup::ethertype.getProtocol(macHeader->getNetworkProtocol());
    packet->addTagIfAbsent<DispatchProtocolReq>()->setProtocol(payloadProtocol);
    packet->addTagIfAbsent<PacketProtocolTag>()->setProtocol(payloadProtocol);
}

} // namespace inet

