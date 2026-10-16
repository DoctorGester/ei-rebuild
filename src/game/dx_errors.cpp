// 00480B80 (A-S8): DirectX result texts; see dx_errors.h. The table is the original's (its
// comparison tree flattened; the texts copied from the exe's strings 00788160-0078A598).
#include "dx_errors.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "originals_library.h"   // CxxThrowException

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <iterator>

namespace {

struct ErrorText {
    std::uint32_t result;
    const char* text;
};

// Sorted by result (unsigned).
constexpr ErrorText kErrorTexts[] = {
    {0x00000000, "No error.\n"},
    {0x80004001, "Action not supported.\n"},
    {0x80004005, "Generic failure.\n"},
    {0x80070005, "Another application has a higher priority level, preventing this call from succeeding\n"},
    {0x8007000C, "The operation cannot be performed unless the device is acquired\n"},
    {0x8007000E, "DirectDraw does not have enough memory to perform the operation.\n"},
    {0x80070015, "This object has not been initialized\n"},
    {0x8007001E, "Access to the input device has been lost. It must be reacquired\n"},
    {0x80070057, "One or more of the parameters passed to the function are incorrect.\n"},
    {0x88760005, "This object is already initialized.\n"},
    {0x8876000A, "This surface can not be attached to the requested surface.\n"},
    {0x88760014, "This surface can not be detached from the requested surface.\n"},
    {0x88760028, "Support is currently not available.\n"},
    {0x88760037, "An exception was encountered while performing the requested operation.\n"},
    {0x8876005A, "Height of rectangle provided is not a multiple of reqd alignment.\n"},
    {0x8876005F, "Unable to match primary surface creation request with existing primary surface.\n"},
    {0x88760064, "One or more of the caps bits passed to the callback are incorrect.\n"},
    {0x8876006E, "DirectDraw does not support the provided cliplist.\n"},
    {0x88760078, "DirectDraw does not support the requested mode.\n"},
    {0x88760082, "DirectDraw received a pointer that was an invalid DIRECTDRAW object.\n"},
    {0x88760091, "The pixel format was invalid as specified.\n"},
    {0x88760096, "Rectangle provided was invalid.\n"},
    {0x887600A0, "Operation could not be carried out because one or more surfaces are locked.\n"},
    {0x887600AA, "There is no 3D present.\n"},
    {0x887600B4, "Operation could not be carried out because there is no alpha accleration hardware present or available.\n"},
    {0x887600CD, "No cliplist available.\n"},
    {0x887600D2, "Operation could not be carried out because there is no color conversion hardware present or available.\n"},
    {0x887600D4, "Create function called without DirectDraw object method SetCooperativeLevel being called.\n"},
    {0x887600D7, "Surface doesn't currently have a color key\n"},
    {0x887600DC, "Operation could not be carried out because there is no hardware support of the destination color key.\n"},
    {0x887600E1, "Operation requires the application to have exclusive mode but the application does not have exclusive mode.\n"},
    {0x887600E6, "Flipping visible surfaces is not supported.\n"},
    {0x887600F0, "There is no GDI present.\n"},
    {0x887600FA, "Operation could not be carried out because there is no hardware present or available.\n"},
    {0x887600FF, "Requested item was not found.\n"},
    {0x88760104, "Operation could not be carried out because there is no overlay hardware present or available.\n"},
    {0x88760118, "Operation could not be carried out because there is no appropriate raster op hardware present or available.\n"},
    {0x88760122, "Operation could not be carried out because there is no rotation hardware present or available.\n"},
    {0x88760136, "Operation could not be carried out because there is no hardware support for stretching.\n"},
    {0x8876013C, "DirectDrawSurface is not in 4 bit color palette and the requested operation requires 4 bit color palette.\n"},
    {0x8876013D, "DirectDrawSurface is not in 4 bit color index palette and the requested operation requires 4 bit color index palette.\n"},
    {0x88760140, "DirectDrawSurface is not in 8 bit color mode and the requested operation requires 8 bit color.\n"},
    {0x8876014A, "Operation could not be carried out because there is no texture mapping hardware present or available.\n"},
    {0x8876014F, "Operation could not be carried out because there is no hardware support for vertical blank synchronized operations.\n"},
    {0x88760154, "Operation could not be carried out because there is no hardware support for zbuffer blitting.\n"},
    {0x8876015E, "Overlay surfaces could not be z layered based on their BltOrder because the hardware does not support z layering of overlays.\n"},
    {0x88760168, "The hardware needed for the requested operation has already been allocated.\n"},
    {0x8876017C, "DirectDraw does not have enough memory to perform the operation.\n"},
    {0x8876017E, "The hardware does not support clipped overlays.\n"},
    {0x88760180, "Can only have ony color key active at one time for overlays.\n"},
    {0x88760183, "Access to this palette is being refused because the palette is already locked by another thread.\n"},
    {0x88760190, "No src color key specified for this operation.\n"},
    {0x8876019A, "This surface is already attached to the surface it is being attached to.\n"},
    {0x887601A4, "This surface is already a dependency of the surface it is being made a dependency of.\n"},
    {0x887601AE, "Access to this surface is being refused because the surface is already locked by another thread.\n"},
    {0x887601B8, "Access to surface refused because the surface is obscured.\n"},
    {0x887601C2, "Access to this surface is being refused because the surface memory is gone. The DirectDrawSurface object representing this surface should have Restore called on it.\n"},
    {0x887601CC, "The requested surface is not attached.\n"},
    {0x887601D6, "Height requested by DirectDraw is too large.\n"},
    {0x887601E0, "Size requested by DirectDraw is too large, but the individual height and width are OK.\n"},
    {0x887601EA, "Width requested by DirectDraw is too large.\n"},
    {0x887601FE, "FOURCC format requested is unsupported by DirectDraw.\n"},
    {0x88760208, "Bitmask in the pixel format requested is unsupported by DirectDraw.\n"},
    {0x88760219, "Vertical blank is in progress.\n"},
    {0x8876021C, "Informs DirectDraw that the previous Blt which is transfering information to or from this Surface is incomplete.\n"},
    {0x88760230, "Rectangle provided was not horizontally aligned on required boundary.\n"},
    {0x88760231, "The GUID passed to the DirectDrawCreate function is not a valid DirectDraw driver identifier\nProbably you have a wrong driver\n"},
    {0x88760232, "A DirectDraw object representing this driver has already been created for this process.\n"},
    {0x88760233, "A hardware-only DirectDraw object creation was attempted but the driver did not support any hardware.\n"},
    {0x88760234, "This process already has created a primary surface.\n"},
    {0x88760235, "Software emulation not available.\n"},
    {0x88760236, "Region passed to Clipper::GetClipList is too small.\n"},
    {0x88760237, "An attempt was made to set a cliplist for a clipper object that is already monitoring an hwnd.\n"},
    {0x88760238, "No clipper object attached to surface object.\n"},
    {0x88760239, "Clipper notification requires an HWND or no HWND has previously been set as the CooperativeLevel HWND.\n"},
    {0x8876023A, "HWND used by DirectDraw CooperativeLevel has been subclassed, this prevents DirectDraw from restoring state.\n"},
    {0x8876023B, "The CooperativeLevel HWND has already been set. It can not be reset while the process has surfaces or palettes created.\n"},
    {0x8876023C, "No palette object attached to this surface.\n"},
    {0x8876023D, "No hardware support for 16 or 256 color palettes.\n"},
    {0x8876023E, "Return if a clipper object is attached to the source surface passed into a BltFast call.\n"},
    {0x8876023F, "No blitter hardware present.\n"},
    {0x88760240, "No DirectDraw ROP hardware.\n"},
    {0x88760241, "Returned when GetOverlayPosition is called on a hidden overlay.\n"},
    {0x88760242, "Returned when GetOverlayPosition is called on an overlay that UpdateOverlay has never been called on to establish a destination.\n"},
    {0x88760243, "Returned when the position of the overlay on the destination is no longer legal for that destination.\n"},
    {0x88760244, "Returned when an overlay member is called for a non-overlay surface.\n"},
    {0x88760245, "An attempt was made to set the cooperative level when it was already set to exclusive.\n"},
    {0x88760246, "An attempt has been made to flip a surface that is not flippable.\n"},
    {0x88760247, "Can't duplicate primary & 3D surfaces, or surfaces that are implicitly created.\n"},
    {0x88760248, "Surface was not locked.  An attempt to unlock a surface that was not locked at all, or by this process, has been attempted.\n"},
    {0x88760249, "Windows can not create any more DCs.\n"},
    {0x8876024A, "No DC was ever created for this surface.\n"},
    {0x8876024B, "This surface can not be restored because it was created in a different mode.\n"},
    {0x8876024C, "This surface can not be restored because it is an implicitly created surface.\n"},
    {0x8876024D, "The surface being used is not a palette-based surface.\n"},
    {0x8876024F, "No mipmap-capable texture mapping hardware is present or available\n"},
    {0x88760276, "An attempt was made to allocate non-local video memory from a device that does not support non-local video memory.\n"},
    {0x887602BC, "D3DERR_BADMAJORVERSION\n"},
    {0x887602BD, "D3DERR_BADMINORVERSION\n"},
    {0x887602C6, "D3DERR_EXECUTE_CREATE_FAILED\n"},
    {0x887602C7, "D3DERR_EXECUTE_DESTROY_FAILED\n"},
    {0x887602C8, "D3DERR_EXECUTE_LOCK_FAILED\n"},
    {0x887602C9, "D3DERR_EXECUTE_UNLOCK_FAILED\n"},
    {0x887602CA, "D3DERR_EXECUTE_LOCKED\n"},
    {0x887602CB, "D3DERR_EXECUTE_NOT_LOCKED\n"},
    {0x887602CC, "D3DERR_EXECUTE_FAILED\n"},
    {0x887602CD, "D3DERR_EXECUTE_CLIPPED_FAILED\n"},
    {0x887602D0, "D3DERR_TEXTURE_NO_SUPPORT\n"},
    {0x887602D1, "D3DERR_TEXTURE_CREATE_FAILED\n"},
    {0x887602D2, "D3DERR_TEXTURE_DESTROY_FAILED\n"},
    {0x887602D3, "D3DERR_TEXTURE_LOCK_FAILED\n"},
    {0x887602D4, "D3DERR_TEXTURE_UNLOCK_FAILED\n"},
    {0x887602D5, "D3DERR_TEXTURE_LOAD_FAILED\n"},
    {0x887602D7, "D3DERR_TEXTURELOCKED\n"},
    {0x887602D8, "D3DERR_TEXTURE_NOT_LOCKED\n"},
    {0x887602DA, "D3DERR_MATRIX_CREATE_FAILED\n"},
    {0x887602DB, "D3DERR_MATRIX_DESTROY_FAILED\n"},
    {0x887602DC, "D3DERR_MATRIX_SETDATA_FAILED\n"},
    {0x887602DE, "D3DERR_SETVIEWPORTDATA_FAILED\n"},
    {0x887602E0, "D3DERR_INVALIDPRIMITIVETYPE\n"},
    {0x887602E1, "D3DERR_INVALIDVERTEXTYPE\n"},
    {0x887602E3, "D3DERR_INVALIDRAMPTEXTURE\n"},
    {0x887602E4, "D3DERR_MATERIAL_CREATE_FAILED\n"},
    {0x887602E5, "D3DERR_MATERIAL_DESTROY_FAILED\n"},
    {0x887602E6, "D3DERR_MATERIAL_SETDATA_FAILED\n"},
    {0x887602EA, "D3DERR_ZBUFF_NEEDS_VIDEOMEMORY\n"},
    {0x887602EB, "The device could not be created because the render target surface is not located in video memory\n"},
    {0x887602EE, "D3DERR_LIGHT_SET_FAILED\n"},
    {0x887602F8, "Previous scene has not been completed\nCall EndScene() first!\n"},
    {0x887602F9, "Attempts to call rendering methods when a scene is not in progress!\nCall BeginScene() first.\n"},
    {0x887602FA, "D3DERR_SCENE_BEGIN_FAILED\n"},
    {0x887602FB, "D3DERR_SCENE_END_FAILED\n"},
    {0x88760305, "The requested operation could not be completed because viewport parameters have not yet been set\n"},
    {0x88760306, "D3DERR_VIEWPORTHASNODEVICE\n"},
    {0x8876080C, "The requested operation could not be completed because the vertex buffer is optimized\n"},
    {0x8876080D, "The vertex buffer could not be created. This can happen when there is insufficient memory to allocate a vertex buffer\n"},
    {0x8876080E, "The requested operation could not be completed because the vertex buffer is locked\n"},
    {0x8876080F, "The vertex buffer could not be unlocked because the vertex buffer memory was overrun.\n Be sure that your application does not write beyond the size of the vertex buffer\n"},
    {0x88760816, "D3DERR_ZBUFFER_NOTPRESENT\n"},
    {0x88760818, "D3DERR_WRONGTEXTUREFORMAT\n"},
    {0x88760819, "D3DERR_UNSUPPORTEDCOLOROPERATION\n"},
    {0x8876081A, "D3DERR_UNSUPPORTEDCOLORARG\n"},
    {0x8876081B, "D3DERR_UNSUPPORTEDALPHAOPERATION\n"},
    {0x8876081C, "D3DERR_UNSUPPORTEDALPHAARG\n"},
    {0x8876081D, "D3DERR_TOOMANYOPERATIONS\n"},
    {0x8876081E, "D3DERR_CONFLICTINGTEXTUREFILTER\n"},
    {0x8876081F, "D3DERR_UNSUPPORTEDFACTORVALUE\n"},
    {0x88760822, "D3DERR_UNSUPPORTEDTEXTUREFILTER\n"},
    {0x88760826, "D3DERR_CONFLICTINGTEXTUREPALETTE\n"},
};

constexpr const char* kUnrecognized = "Unrecognized error value.\n";

} // namespace

const char* __fastcall DirectXErrorText(long result) {
    const std::uint32_t key = static_cast<std::uint32_t>(result);
    const ErrorText* found = std::lower_bound(std::begin(kErrorTexts), std::end(kErrorTexts), key,
                                              [](const ErrorText& e, std::uint32_t k) { return e.result < k; });
    return found != std::end(kErrorTexts) && found->result == key ? found->text : kUnrecognized;
}
EI_REPLACE(0x00480B80, DirectXErrorText);

// ---- The thrown error object (00480250-004815D0) ---------------------------------------------------

static_assert(sizeof(GameError) == 0xC);

namespace {

void* ErrorVtable() { return reinterpret_cast<void*>(0x0073CAB4); }
void* ErrorBaseVtable() { return reinterpret_cast<void*>(0x0073CAB0); }
// 00787EE4: "%s (0x%.8X) %s".
const char* ResultFormat() { return reinterpret_cast<const char*>(0x00787EE4); }

// The original's stack buffers (wvsprintf writes at most 1024 characters; the sprintf of 00480300
// can run past its 1024 bytes, as in the original).
constexpr std::size_t kBuffer = 0x400;

// The message: an operator new copy of `text`.
char* CopyMessage(const char* text) {
    std::size_t size = std::strlen(text) + 1;
    auto* message = static_cast<char*>(gamecrt::operator_new(size));
    std::memcpy(message, text, size);
    return message;
}

} // namespace

GameError* GameErrorFormat(GameError* error, const char* format, va_list args) {
    error->message = nullptr;
    error->vtable = ErrorVtable();
    char text[kBuffer];
    gameapi::wvsprintfA(text, format, args);
    error->message = CopyMessage(text);
    return error;
}

GameError* __cdecl GameErrorConstruct(GameError* error, const char* format, ...) {
    va_list args;
    va_start(args, format);
    GameErrorFormat(error, format, args);
    va_end(args);
    return error;
}
EI_REPLACE(0x00480250, GameErrorConstruct);

GameError* GameErrorFormatResult(GameError* error, long result, const char* format, va_list args) {
    error->message = nullptr;
    error->vtable = ErrorVtable();
    char text[kBuffer];
    char full[kBuffer];
    gameapi::wvsprintfA(text, format, args);
    error->result = result;
    gamecrt::sprintf(full, ResultFormat(), text, result, DirectXErrorText(result));
    error->message = CopyMessage(full);
    return error;
}

GameError* __cdecl GameErrorConstructResult(GameError* error, long result, const char* format, ...) {
    va_list args;
    va_start(args, format);
    GameErrorFormatResult(error, result, format, args);
    va_end(args);
    return error;
}
EI_REPLACE(0x00480300, GameErrorConstructResult);

namespace {
[[noreturn]] void Throw(GameError* error) {
    libcrt::CxxThrowException(&error, reinterpret_cast<const void*>(0x00759908));
    for (;;) {
    }
}
} // namespace

void ThrowGameError(const char* format, ...) {
    auto* error = static_cast<GameError*>(gamecrt::operator_new(sizeof(GameError)));
    va_list args;
    va_start(args, format);
    if (error) GameErrorFormat(error, format, args);
    va_end(args);
    Throw(error);
}

void ThrowGameErrorResult(long result, const char* format, ...) {
    auto* error = static_cast<GameError*>(gamecrt::operator_new(sizeof(GameError)));
    va_list args;
    va_start(args, format);
    if (error) GameErrorFormatResult(error, result, format, args);
    va_end(args);
    Throw(error);
}

void __fastcall GameErrorDestruct(GameError* error) {
    error->vtable = ErrorBaseVtable();
    if (error->message) gamecrt::operator_delete(error->message);
}
EI_REPLACE(0x004815D0, GameErrorDestruct);

GameError* GameError::DeleteScalar(int flags) {
    GameErrorDestruct(this);
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x004815B0, GameError::DeleteScalar);

GameError* GameError::DeleteScalarBase(int flags) {
    GameErrorDestruct(this);
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x00481580, GameError::DeleteScalarBase);
