#pragma once

// 00669D50 (CPlayerClient::OnGameState), the server's game state message, also sets the client's
// tick to the server's (MOV [007AF61C], the client 007AF5B8 + 0x64). Its verify wrapper
// (verify_player_client_handlers.cpp) counts the messages and keeps the tick after each, so that
// CWorldClient::Segment's check (verify_world_client.cpp) knows the tick it started from.
namespace game_state_message {
extern int g_count;
extern int g_tickAfter;
} // namespace game_state_message
