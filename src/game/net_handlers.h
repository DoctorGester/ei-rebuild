#pragma once

// The message handlers the sessions register (net_client.cpp, net_server.cpp): the game client's
// (net_client_handlers.cpp) and the game server's (net_server_*.cpp). Each is called with the session and
// the received message; the session keeps the message when the handler stores it.

#include "net_classes.h"
#include "net_messages.h"

namespace net {

void __fastcall ClientOnAward(ClientSession*, Message*);
void __fastcall ClientOnBinding(ClientSession*, BindingMessage*);
void __fastcall ClientOnBindingGone(ClientSession*, Message*);
void __fastcall ClientOnFile(ClientSession*, FileMessage*);
void __fastcall ClientOnFileChunk(ClientSession*, FileChunkMessage*);
void __fastcall ClientOnFileGone(ClientSession*, Message*);
void __fastcall ClientOnInfo(ClientSession*, HostMessage*);
void __fastcall ClientOnInfoGone(ClientSession*, Message*);
void __fastcall ClientOnLine(ClientSession*, LineMessage*);
void __fastcall ClientOnLineGone(ClientSession*, Message*);
void __fastcall ClientOnMap(ClientSession*, MapMessage*);
void __fastcall ClientOnMapGone(ClientSession*, Message*);
void __fastcall ClientOnMember(ClientSession*, Message*);
void __fastcall ClientOnObjectGone(ClientSession*, Message*);
void __fastcall ClientOnParty(ClientSession*, Message*);
void __fastcall ClientOnPartyChanged(ClientSession*, Message*);
void __fastcall ClientOnPartyGone(ClientSession*, Message*);
void __fastcall ClientOnPlayer(ClientSession*, Message*);
void __fastcall ClientOnPlayerChanged(ClientSession*, Message*);
void __fastcall ClientOnPlayerGone(ClientSession*, Message*);
void __fastcall ClientOnTrade(ClientSession*, TradeMessage*);
void __fastcall ClientOnTradeChanged(ClientSession*, TradeMessage*);
void __fastcall ClientOnTradeGone(ClientSession*, Message*);
void __fastcall ClientOnVariable(ClientSession*, Message*);
void __fastcall ClientOnVariableChanged(ClientSession*, Message*);
void __fastcall ClientOnVariableGone(ClientSession*, Message*);
void __fastcall ClientOnZone(ClientSession*, ZoneMessage*);
void __fastcall ClientOnZoneGone(ClientSession*, ZoneMessage*);
void __fastcall ServerOnCommand(ServerSession*, CommandMessage*);
void __fastcall ServerOnJoin(ServerSession*, LoginMessage*);
void __fastcall ServerOnMemberChanged(ServerSession*, Message*);
void __fastcall ServerOnObjectAdded(ServerSession*, FileRequestMessage*);
void __fastcall ServerOnObjectRemoved(ServerSession*, Message*);
void __fastcall ServerOnPlayerChanged(ServerSession*, LoginMessage*);
void __fastcall ServerOnPlayerGone(ServerSession*, LoginMessage*);
void __fastcall ServerOnSay(ServerSession*, SayMessage*);
void __fastcall ServerOnTradeCancel(ServerSession*, PartyMessage*);
void __fastcall ServerOnTradeOffer(ServerSession*, PartyMessage*);
void __fastcall ServerOnTradeUpdate(ServerSession*, PartyMessage*);

} // namespace net
