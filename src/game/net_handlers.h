#pragma once

// The message handlers the sessions register (net_client.cpp, net_server.cpp): the game client's
// (net_client_handlers.cpp) and the game server's (net_server_*.cpp). Each is called with the session and
// the received message; the session keeps the message when the handler stores it.

#include "net_classes.h"
#include "net_messages.h"

namespace net {

void ClientOnAward(ClientSession*, Message*);
void ClientOnBinding(ClientSession*, BindingMessage*);
void ClientOnBindingGone(ClientSession*, Message*);
void ClientOnFile(ClientSession*, FileMessage*);
void ClientOnFileChunk(ClientSession*, FileChunkMessage*);
void ClientOnFileGone(ClientSession*, Message*);
void ClientOnInfo(ClientSession*, HostMessage*);
void ClientOnInfoGone(ClientSession*, Message*);
void ClientOnLine(ClientSession*, LineMessage*);
void ClientOnLineGone(ClientSession*, Message*);
void ClientOnMap(ClientSession*, MapMessage*);
void ClientOnMapGone(ClientSession*, Message*);
void ClientOnMember(ClientSession*, Message*);
void ClientOnObjectGone(ClientSession*, Message*);
void ClientOnParty(ClientSession*, Message*);
void ClientOnPartyChanged(ClientSession*, Message*);
void ClientOnPartyGone(ClientSession*, Message*);
void ClientOnPlayer(ClientSession*, Message*);
void ClientOnPlayerChanged(ClientSession*, Message*);
void ClientOnPlayerGone(ClientSession*, Message*);
void ClientOnTrade(ClientSession*, TradeMessage*);
void ClientOnTradeChanged(ClientSession*, TradeMessage*);
void ClientOnTradeGone(ClientSession*, Message*);
void ClientOnVariable(ClientSession*, Message*);
void ClientOnVariableChanged(ClientSession*, Message*);
void ClientOnVariableGone(ClientSession*, Message*);
void ClientOnZone(ClientSession*, ZoneMessage*);
void ClientOnZoneGone(ClientSession*, ZoneMessage*);
void ServerOnCommand(ServerSession*, CommandMessage*);
void ServerOnJoin(ServerSession*, LoginMessage*);
void ServerOnMemberChanged(ServerSession*, Message*);
void ServerOnObjectAdded(ServerSession*, FileRequestMessage*);
void ServerOnObjectRemoved(ServerSession*, Message*);
void ServerOnPlayerChanged(ServerSession*, LoginMessage*);
void ServerOnPlayerGone(ServerSession*, LoginMessage*);
void ServerOnSay(ServerSession*, SayMessage*);
void ServerOnTradeCancel(ServerSession*, PartyMessage*);
void ServerOnTradeOffer(ServerSession*, PartyMessage*);
void ServerOnTradeUpdate(ServerSession*, PartyMessage*);

} // namespace net
