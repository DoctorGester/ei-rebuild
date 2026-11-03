"""A harness run's game processes for tools/run_all.py: start, a group that dies with this process, ending, and what a
game that will not end shows (its open windows, where its threads wait).

Windows: the games are in a job object that is killed when its last handle closes, so no game outlives run_all, even
when run_all itself is killed. POSIX: each game leads a session of its own (its process group) and gets SIGKILL when the
thread that started it ends (PR_SET_PDEATHSIG; run_all's game threads live until their game has ended).
"""
import ctypes, os, signal, subprocess, threading

WINDOWS = os.name == 'nt'

if WINDOWS:
    from ctypes import wintypes as w

    _k32 = ctypes.WinDLL('kernel32', use_last_error=True)
    _u32 = ctypes.WinDLL('user32', use_last_error=True)
    _HANDLE = ctypes.c_void_p

    class _BasicLimits(ctypes.Structure):
        _fields_ = [('PerProcessUserTimeLimit', ctypes.c_longlong), ('PerJobUserTimeLimit', ctypes.c_longlong),
                    ('LimitFlags', w.DWORD), ('MinimumWorkingSetSize', ctypes.c_size_t),
                    ('MaximumWorkingSetSize', ctypes.c_size_t), ('ActiveProcessLimit', w.DWORD),
                    ('Affinity', ctypes.c_size_t), ('PriorityClass', w.DWORD), ('SchedulingClass', w.DWORD)]

    class _ExtendedLimits(ctypes.Structure):
        _fields_ = [('BasicLimitInformation', _BasicLimits), ('IoInfo', ctypes.c_ulonglong * 6),
                    ('ProcessMemoryLimit', ctypes.c_size_t), ('JobMemoryLimit', ctypes.c_size_t),
                    ('PeakProcessMemoryUsed', ctypes.c_size_t), ('PeakJobMemoryUsed', ctypes.c_size_t)]

    class _ModuleEntry(ctypes.Structure):
        _fields_ = [('dwSize', w.DWORD), ('th32ModuleID', w.DWORD), ('th32ProcessID', w.DWORD),
                    ('GlblcntUsage', w.DWORD), ('ProccntUsage', w.DWORD), ('modBaseAddr', ctypes.c_void_p),
                    ('modBaseSize', w.DWORD), ('hModule', ctypes.c_void_p), ('szModule', ctypes.c_wchar * 256),
                    ('szExePath', ctypes.c_wchar * 260)]

    class _ThreadEntry(ctypes.Structure):
        _fields_ = [('dwSize', w.DWORD), ('cntUsage', w.DWORD), ('th32ThreadID', w.DWORD),
                    ('th32OwnerProcessID', w.DWORD), ('tpBasePri', w.LONG), ('tpDeltaPri', w.LONG), ('dwFlags', w.DWORD)]

    for name, res, args in (
            ('CreateJobObjectW', _HANDLE, (ctypes.c_void_p, ctypes.c_wchar_p)),
            ('SetInformationJobObject', w.BOOL, (_HANDLE, ctypes.c_int, ctypes.c_void_p, w.DWORD)),
            ('AssignProcessToJobObject', w.BOOL, (_HANDLE, _HANDLE)),
            ('TerminateJobObject', w.BOOL, (_HANDLE, w.UINT)),
            ('CloseHandle', w.BOOL, (_HANDLE,)),
            ('CreateToolhelp32Snapshot', _HANDLE, (w.DWORD, w.DWORD)),
            ('Module32FirstW', w.BOOL, (_HANDLE, ctypes.POINTER(_ModuleEntry))),
            ('Module32NextW', w.BOOL, (_HANDLE, ctypes.POINTER(_ModuleEntry))),
            ('Thread32First', w.BOOL, (_HANDLE, ctypes.POINTER(_ThreadEntry))),
            ('Thread32Next', w.BOOL, (_HANDLE, ctypes.POINTER(_ThreadEntry))),
            ('OpenThread', _HANDLE, (w.DWORD, w.BOOL, w.DWORD)),
            ('SuspendThread', w.DWORD, (_HANDLE,)),
            ('ResumeThread', w.DWORD, (_HANDLE,)),
            ('GetThreadContext', w.BOOL, (_HANDLE, ctypes.c_void_p)),
            ('ReadProcessMemory', w.BOOL, (_HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t,
                                           ctypes.POINTER(ctypes.c_size_t)))):
        getattr(_k32, name).restype, getattr(_k32, name).argtypes = res, args

    _ENUM = ctypes.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
    _u32.EnumWindows.argtypes = (_ENUM, w.LPARAM)
    _u32.EnumChildWindows.argtypes = (w.HWND, _ENUM, w.LPARAM)
    _u32.GetWindowThreadProcessId.argtypes = (w.HWND, ctypes.POINTER(w.DWORD))
    _u32.IsWindowVisible.argtypes = (w.HWND,)
    _u32.GetClassNameW.argtypes = (w.HWND, w.LPWSTR, ctypes.c_int)
    _u32.GetWindowTextW.argtypes = (w.HWND, w.LPWSTR, ctypes.c_int)
    _INVALID = ctypes.c_void_p(-1).value


def _die_with_parent():
    """POSIX child side: SIGKILL when the starting thread ends (Linux; elsewhere nothing)."""
    try:
        ctypes.CDLL(None).prctl(1, signal.SIGKILL)   # PR_SET_PDEATHSIG
    except (AttributeError, OSError):
        pass


class Group:
    """The games of one run: they end together when end() is called, and with this process."""

    def __init__(self):
        self.procs = []
        self.job = None
        if WINDOWS:
            self.job = _k32.CreateJobObjectW(None, None)
            limits = _ExtendedLimits()
            limits.BasicLimitInformation.LimitFlags = 0x2000   # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
            if self.job and not _k32.SetInformationJobObject(self.job, 9, ctypes.byref(limits), ctypes.sizeof(limits)):
                _k32.CloseHandle(self.job)
                self.job = None

    def start(self, cmd, cwd, env, output):
        """Starts cmd (its stdout and stderr to the open file `output`) in the group; the Popen."""
        if WINDOWS:
            proc = subprocess.Popen(cmd, cwd=cwd, env=env, stdin=subprocess.DEVNULL, stdout=output, stderr=output)
            if self.job:
                _k32.AssignProcessToJobObject(self.job, int(proc._handle))
        else:
            proc = subprocess.Popen(cmd, cwd=cwd, env=env, stdin=subprocess.DEVNULL, stdout=output, stderr=output,
                                    start_new_session=True, preexec_fn=_die_with_parent)
        self.procs.append(proc)
        return proc

    def kill(self, proc):
        """Ends proc and whatever it started."""
        _quietly(proc.kill)
        if not WINDOWS:
            _quietly(os.killpg, proc.pid, signal.SIGKILL)

    def close(self):
        """Ends every game of the group still running (and their children)."""
        if WINDOWS:
            if self.job:
                _k32.TerminateJobObject(self.job, 1)
                _k32.CloseHandle(self.job)
                self.job = None
        else:
            for proc in self.procs:
                _quietly(os.killpg, proc.pid, signal.SIGKILL)


def _quietly(fn, *args):
    try:
        fn(*args)
    except OSError:
        pass


def open_windows(pid):
    """The visible windows of the process, one line each: class, title and the text of its child controls (a dialog
    the game waits on says why it does not end). Empty on POSIX."""
    if not WINDOWS:
        return []
    lines = []

    def text_of(hwnd, size=512):
        buf = ctypes.create_unicode_buffer(size)
        _u32.GetWindowTextW(hwnd, buf, size)
        return buf.value

    def top(hwnd, _):
        owner = w.DWORD()
        _u32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value != pid or not _u32.IsWindowVisible(hwnd):
            return True
        cls = ctypes.create_unicode_buffer(64)
        _u32.GetClassNameW(hwnd, cls, 64)
        parts = []
        _u32.EnumChildWindows(hwnd, _ENUM(lambda child, _: bool(parts.append(text_of(child))) or True), 0)
        children = ' | '.join(p for p in parts if p).replace('\r', ' ').replace('\n', ' ')
        lines.append(f"[{cls.value}] '{text_of(hwnd, 256)}' {children}")
        return True
    _u32.EnumWindows(_ENUM(top), 0)
    return lines


def open_windows_bounded(pid, seconds=2.0):
    """open_windows on a thread of its own: whatever the game does, it cannot hold the caller longer than seconds."""
    found = []
    t = threading.Thread(target=lambda: found.extend(open_windows(pid)), daemon=True)
    t.start()
    t.join(seconds)
    return list(found)


def thread_stacks(proc):
    """Where a game that will not end waits: for each thread the instruction pointer and the return addresses on its
    stack (words after a CALL into a loaded module), as module+offset. The game's own watchdog cannot write this once
    exit() is under way (ExitProcess has ended every other thread). POSIX: each thread's kernel wait channel."""
    if not WINDOWS:
        lines = []
        for tid in sorted(os.listdir(f'/proc/{proc.pid}/task')) if os.path.isdir(f'/proc/{proc.pid}/task') else []:
            try:
                with open(f'/proc/{proc.pid}/task/{tid}/wchan') as f:
                    lines.append(f'thread {tid}: waits in {f.read().strip() or "?"}')
            except OSError:
                pass
        return '\n'.join(lines) + '\n'
    process, pid = _HANDLE(int(proc._handle)), proc.pid
    modules = []
    snap = _k32.CreateToolhelp32Snapshot(0x8, pid)   # TH32CS_SNAPMODULE
    if snap and snap != _INVALID:
        me = _ModuleEntry(dwSize=ctypes.sizeof(_ModuleEntry))
        ok = _k32.Module32FirstW(snap, ctypes.byref(me))
        while ok:
            modules.append((me.modBaseAddr or 0, me.modBaseSize, me.szModule))
            ok = _k32.Module32NextW(snap, ctypes.byref(me))
        _k32.CloseHandle(snap)

    def describe(a):
        return next((f'{name}+{a - base:X}' for base, size, name in modules if 0 <= a - base < size), None)

    def read(address, size):
        buf = ctypes.create_string_buffer(size)
        got = ctypes.c_size_t()
        _k32.ReadProcessMemory(process, ctypes.c_void_p(address), buf, size, ctypes.byref(got))
        return buf.raw[:got.value]

    def after_call(a):
        # A return address: the 2, 3, 5, 6 or 7 bytes before it are a CALL (E8 rel32, FF /2 register or memory).
        b = read(a - 8, 8)
        if len(b) != 8:
            return False
        return b[3] == 0xE8 or any(b[8 - n] == 0xFF and (b[9 - n] >> 3) & 7 == 2 for n in (2, 3, 6, 7))

    out = []
    snap = _k32.CreateToolhelp32Snapshot(0x4, 0)   # TH32CS_SNAPTHREAD
    te = _ThreadEntry(dwSize=ctypes.sizeof(_ThreadEntry))
    ok = snap and snap != _INVALID and _k32.Thread32First(snap, ctypes.byref(te))
    # CONTEXT (x64): 1232 bytes, 16-byte aligned; ContextFlags at 0x30, Rsp at 0x98, Rip at 0xF8.
    context = ctypes.create_string_buffer(1232 + 16)
    at = (ctypes.addressof(context) + 15) & ~15
    while ok:
        if te.th32OwnerProcessID == pid:
            thread = _k32.OpenThread(0x2 | 0x8 | 0x40, False, te.th32ThreadID)
            if thread:
                _k32.SuspendThread(thread)
                ctypes.memset(at, 0, 1232)
                ctypes.c_uint32.from_address(at + 0x30).value = 0x100001   # CONTEXT_AMD64 | CONTEXT_CONTROL
                if _k32.GetThreadContext(thread, ctypes.c_void_p(at)):
                    ip = ctypes.c_uint64.from_address(at + 0xF8).value
                    sp = ctypes.c_uint64.from_address(at + 0x98).value
                    out.append(f'thread {te.th32ThreadID}: rip {ip:016X} {describe(ip) or "?"}')
                    stack = b''
                    for size in (128 * 1024, 64 * 1024, 16 * 1024, 4096):   # the read stops at the stack's end
                        stack = read(sp, size)
                        if stack:
                            break
                    shown = 0
                    for i in range(0, len(stack) - 7, 8):
                        word = int.from_bytes(stack[i:i + 8], 'little')
                        if shown < 48 and (where := describe(word)) and after_call(word):
                            out.append(f'  {where}')
                            shown += 1
                _k32.ResumeThread(thread)
                _k32.CloseHandle(thread)
        ok = _k32.Thread32Next(snap, ctypes.byref(te))
    if snap and snap != _INVALID:
        _k32.CloseHandle(snap)
    return '\n'.join(out) + '\n'
