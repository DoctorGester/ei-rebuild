#pragma once

// The prototypes of game.exe's imports that the Windows SDK headers we compile against do not declare, or declare
// differently: Miles (mss32.dll), Bink (binkw32.dll), Winsock 1.1 by ordinal (WSOCK32.dll), DirectInput,
// DirectDraw, DirectSound's enumerator, the common controls, comdlg32, the print spooler, GetVersion (deprecated in
// the SDK) and the Interlocked pair (macros for the compiler's intrinsics there). tools/gen_iat.py types
// lib/import_table.h's ImportTable member of each `using` here's name with it; gameapi.h calls them through the
// slot. A Winsock SOCKET is a std::uint32_t, its addresses and sets untyped.

#include <windows.h>

#include <cstdint>

// Miles Sound System's handles (HSAMPLE, HSEQUENCE, HDIGDRIVER, HMDIDRIVER), its AILSOUNDINFO (defined where
// AIL_WAV_info's caller reads it) and the end-of-sample callback.
namespace miles {
struct Sample;
struct Sequence;
struct DigitalDriver;
struct MidiDriver;
struct SoundInfo;
using SampleCallback = void(__stdcall*)(Sample*);
using SequenceCallback = void(__stdcall*)(Sequence*);
} // namespace miles

// Bink's handle (HBINK), as far as the game reads it (game/bink_video.h).
struct BinkHeader;

namespace ei::import_fn {

// mss32.dll
using AIL_set_sample_file = int(__stdcall*)(miles::Sample*, const void* file, int block);
using AIL_WAV_info = int(__stdcall*)(const void* data, miles::SoundInfo* info);
using AIL_mem_alloc_lock = void*(__stdcall*)(int size);
using AIL_register_EOS_callback = miles::SampleCallback(__stdcall*)(miles::Sample*, miles::SampleCallback);
using AIL_set_named_sample_file = int(__stdcall*)(miles::Sample*, const char* suffix, const void* file, int size,
                                                  int block);
using AIL_set_sample_reverb = void(__stdcall*)(miles::Sample*, float level, float reflectTime, float decayTime);
using AIL_set_sample_ms_position = void(__stdcall*)(miles::Sample*, std::uint32_t ms);
using AIL_start_sample = void(__stdcall*)(miles::Sample*);
using AIL_sample_position = std::int32_t(__stdcall*)(miles::Sample*);
using AIL_set_sample_volume = void(__stdcall*)(miles::Sample*, int volume);
using AIL_set_sample_loop_count = void(__stdcall*)(miles::Sample*, int count);
using AIL_set_sample_pan = void(__stdcall*)(miles::Sample*, int pan);
using AIL_resume_sample = void(__stdcall*)(miles::Sample*);
using AIL_stop_sample = void(__stdcall*)(miles::Sample*);
using AIL_release_sample_handle = void(__stdcall*)(miles::Sample*);
using AIL_set_sample_processor = void*(__stdcall*)(miles::Sample*, int stage, void* provider);
using AIL_set_sample_position = void(__stdcall*)(miles::Sample*, int position);
using AIL_release_sequence_handle = void(__stdcall*)(miles::Sequence*);
using AIL_waveOutClose = void(__stdcall*)(miles::DigitalDriver*);
using AIL_midiOutClose = void(__stdcall*)(miles::MidiDriver*);
using AIL_set_preference = int(__stdcall*)(unsigned number, int value);
using AIL_waveOutOpen = int(__stdcall*)(miles::DigitalDriver** driver, void* waveOut, int device, const void* format);
using AIL_last_error = const char*(__stdcall*)();
using AIL_set_DirectSound_HWND = void(__stdcall*)(miles::DigitalDriver*, void* window);
using AIL_allocate_sample_handle = miles::Sample*(__stdcall*)(miles::DigitalDriver*);
using AIL_sample_user_data = std::intptr_t(__stdcall*)(miles::Sample*, unsigned index);
using AIL_set_sample_playback_rate = void(__stdcall*)(miles::Sample*, int rate);
using AIL_register_sequence_callback = miles::SequenceCallback(__stdcall*)(miles::Sequence*, miles::SequenceCallback);
using AIL_set_sequence_user_data = void(__stdcall*)(miles::Sequence*, unsigned index, std::intptr_t value);
using AIL_init_sequence = int(__stdcall*)(miles::Sequence*, const void* data, int number);
using AIL_set_sample_user_data = void(__stdcall*)(miles::Sample*, unsigned index, std::intptr_t value);
using AIL_get_DirectSound_info = void(__stdcall*)(miles::Sample*, void** directSound, void** buffer);
using AIL_shutdown = void(__stdcall*)();
using AIL_startup = int(__stdcall*)();
using AIL_end_sample = void(__stdcall*)(miles::Sample*);
using AIL_set_sample_type = void(__stdcall*)(miles::Sample*, int format, int flags);
using AIL_minimum_sample_buffer_size = int(__stdcall*)(miles::DigitalDriver*, int rate, int format);
using RIB_find_file_provider = void*(__stdcall*)(const char* interfaceName, const char* property, const char* suffix);
using AIL_sample_buffer_ready = int(__stdcall*)(miles::Sample*);
using AIL_load_sample_buffer = void(__stdcall*)(miles::Sample*, int half, void* buffer, int length);
using AIL_start_sequence = void(__stdcall*)(miles::Sequence*);
using AIL_sequence_user_data = std::intptr_t(__stdcall*)(miles::Sequence*, unsigned index);
using AIL_sequence_ms_position = void(__stdcall*)(miles::Sequence*, int* total, int* current);
using AIL_mem_free_lock = void(__stdcall*)(void* memory);
using AIL_end_sequence = void(__stdcall*)(miles::Sequence*);
// (data, size, the file name's suffix, decoded data, its size, the progress callback)
using AIL_decompress_ASI = int(__stdcall*)(const void* data, int size, const char* suffix, void** decoded,
                                           int* decodedSize, void* callback);
using AIL_init_sample = void(__stdcall*)(miles::Sample*);

// binkw32.dll. BinkSetSoundSystem takes BinkOpenMiles itself (and the Miles driver).
using BinkGoto = void(__stdcall*)(BinkHeader*, unsigned frame, int flags);
using BinkDoFrame = int(__stdcall*)(BinkHeader*);
using BinkDDSurfaceType = int(__stdcall*)(void* surface);
using BinkOpen = BinkHeader*(__stdcall*)(const char* name, unsigned flags);
using BinkNextFrame = void(__stdcall*)(BinkHeader*);
using BinkWait = int(__stdcall*)(BinkHeader*);
using BinkSetFrameRate = void(__stdcall*)(unsigned rate, unsigned divider);
using BinkSetVolume = void(__stdcall*)(BinkHeader*, int volume);
using BinkOpenMiles = void*(__stdcall*)(std::uint32_t driver);
using BinkSetSoundSystem = int(__stdcall*)(BinkOpenMiles open, miles::DigitalDriver* driver);
using BinkClose = void(__stdcall*)(BinkHeader*);
using BinkCopyToBuffer = int(__stdcall*)(BinkHeader*, void* to, int pitch, int height, int x, int y, unsigned flags);

// WSOCK32.dll (Winsock 1.1, by ordinal).
using listen = int(__stdcall*)(std::uint32_t s, int backlog);
using accept = std::uint32_t(__stdcall*)(std::uint32_t s, void* address, int* length);
using WSAFDIsSet = int(__stdcall*)(std::uint32_t s, void* set);
using select = int(__stdcall*)(int count, void* read, void* write, void* except, const void* timeout);
using connect = int(__stdcall*)(std::uint32_t s, const void* address, int length);
using recvfrom = int(__stdcall*)(std::uint32_t s, void* data, int length, int flags, void* from, int* fromLength);
using send = int(__stdcall*)(std::uint32_t s, const void* data, int length, int flags);
using socket = std::uint32_t(__stdcall*)(int family, int type, int protocol);
using recv = int(__stdcall*)(std::uint32_t s, void* data, int length, int flags);
using bind = int(__stdcall*)(std::uint32_t s, const void* address, int length);
using setsockopt = int(__stdcall*)(std::uint32_t s, int level, int option, const void* value, int length);
using closesocket = int(__stdcall*)(std::uint32_t s);
using WSACleanup = int(__stdcall*)();
using WSAStartup = int(__stdcall*)(std::uint16_t version, void* data);
using gethostname = int(__stdcall*)(char* name, int length);
using gethostbyaddr = void*(__stdcall*)(const void* address, int length, int type);
using ntohs = std::uint16_t(__stdcall*)(std::uint16_t v);
using inet_addr = std::uint32_t(__stdcall*)(const char* text);
using gethostbyname = void*(__stdcall*)(const char* name);
using htons = std::uint16_t(__stdcall*)(std::uint16_t v);
using ioctlsocket = int(__stdcall*)(std::uint32_t s, long command, std::uint32_t* argument);
using sendto = int(__stdcall*)(std::uint32_t s, const void* data, int length, int flags, const void* to, int toLength);

// DINPUT.dll, DDRAW.dll, DSOUND.dll (#2) and COMCTL32.dll (#17).
using DirectInputCreateEx = long(__stdcall*)(void* instance, unsigned long version, const void* iid, void** out,
                                             void* outer);
using DirectDrawEnumerateExA = long(__stdcall*)(void* callback, void* context, unsigned long flags);
using DirectDrawCreateEx = long(__stdcall*)(void* guid, void** out, const void* iid, void* outer);
using DirectSoundEnumerateA = long(__stdcall*)(void* callback, void* context);
using InitCommonControls = void(__stdcall*)();

// WINSPOOL.DRV and comdlg32.dll.
using ClosePrinter = BOOL(WINAPI*)(HANDLE printer);
using DocumentPropertiesA = LONG(WINAPI*)(HWND window, HANDLE printer, char* device, DEVMODEA* out, DEVMODEA* in,
                                          DWORD mode);
using OpenPrinterA = BOOL(WINAPI*)(char* name, HANDLE* printer, void* defaults);
using GetFileTitleA = short(WINAPI*)(const char* path, char* title, WORD size);

// KERNEL32.dll.
using GetVersion = DWORD(WINAPI*)();
using InterlockedIncrement = LONG(WINAPI*)(LONG volatile* value);
using InterlockedDecrement = LONG(WINAPI*)(LONG volatile* value);

} // namespace ei::import_fn
