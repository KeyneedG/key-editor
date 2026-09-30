[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Executable,
    [Parameter(Mandatory = $true)][string]$WorkingDirectory,
    [string[]]$Arguments = @(),
    [switch]$Wait
)

$ErrorActionPreference = 'Stop'
try {
    # CreateProcess with handle inheritance disabled detaches from Ninja/MSBuild.
    # A console is omitted; the project's own Win32 window still opens normally.
    if (-not ('Tiny3D.NativeLauncher' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;
namespace Tiny3D {
    public static class NativeLauncher {
        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        struct StartupInfo {
            public uint size;
            public string reserved, desktop, title;
            public uint x, y, width, height, xChars, yChars, fill, flags;
            public ushort show, reservedSize;
            public IntPtr reservedData, input, output, error;
        }
        [StructLayout(LayoutKind.Sequential)]
        struct ProcessInfo {
            public IntPtr process, thread;
            public uint processId, threadId;
        }
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        static extern bool CreateProcess(string application, StringBuilder command,
            IntPtr processAttributes, IntPtr threadAttributes, bool inheritHandles,
            uint flags, IntPtr environment, string directory, ref StartupInfo startup,
            out ProcessInfo process);
        [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
        [DllImport("kernel32.dll")] static extern uint WaitForSingleObject(IntPtr handle, uint milliseconds);
        [DllImport("kernel32.dll")] static extern bool GetExitCodeProcess(IntPtr handle, out uint code);

        public static uint Start(string executable, string arguments, string directory, bool wait) {
            var startup = new StartupInfo();
            startup.size = (uint)Marshal.SizeOf(startup);
            ProcessInfo process;
            var command = new StringBuilder("\"" + executable + "\" " + arguments);
            // CREATE_NO_WINDOW, bInheritHandles=false: no console or build output pipes.
            if (!CreateProcess(executable, command, IntPtr.Zero, IntPtr.Zero, false,
                0x08000000, IntPtr.Zero, directory, ref startup, out process)) {
                throw new Win32Exception(Marshal.GetLastWin32Error());
            }
            try {
                if (wait) {
                    WaitForSingleObject(process.process, 0xffffffff);
                    uint code;
                    if (!GetExitCodeProcess(process.process, out code))
                        throw new Win32Exception(Marshal.GetLastWin32Error());
                    if (code != 0) throw new InvalidOperationException("Project exited with code " + code);
                }
                return process.processId;
            } finally {
                CloseHandle(process.thread);
                CloseHandle(process.process);
            }
        }
    }
}
'@
    }
    # Quote argv values directly; no shell interprets the arguments.
    $quotedArguments = ($Arguments | ForEach-Object {
        '"' + ($_ -replace '(\\*)"', '$1$1\"' -replace '(\\+)$', '$1$1') + '"'
    }) -join ' '
    $projectProcessId = [Tiny3D.NativeLauncher]::Start($Executable, $quotedArguments, $WorkingDirectory, $Wait.IsPresent)
    Write-Host "Started selected project (PID $projectProcessId)."
} catch {
    Write-Error $_ -ErrorAction Continue
    exit 1
}
