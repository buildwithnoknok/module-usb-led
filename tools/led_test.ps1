# noknok LEDs â€” quick command test over the CDC COM port (raw handle, bypasses
# PowerShell SerialPort quirks). Usage:  .\led_test.ps1 -Port COM12
param([string]$Port = "COM12")

$src = @'
using System;
using System.IO;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
public static class ComRaw {
  [DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Auto)]
  static extern SafeFileHandle CreateFile(string name, uint access, uint share,
      IntPtr sa, uint disp, uint flags, IntPtr templ);
  public static FileStream Open(string port){
    var h = CreateFile(@"\\.\"+port, 0xC0000000, 0, IntPtr.Zero, 3, 0x80, IntPtr.Zero);
    if (h.IsInvalid) throw new IOException("CreateFile failed, err="+Marshal.GetLastWin32Error());
    return new FileStream(h, FileAccess.ReadWrite);
  }
}
'@
Add-Type -TypeDefinition $src

$fs = [ComRaw]::Open($Port)
function Send([byte[]]$b){ $fs.Write($b,0,$b.Length); $fs.Flush() }

Write-Host "Port $Port open. Cycling colors..."
Send 0x01,0,40,0;  Start-Sleep -Milliseconds 800   # all green
Send 0x01,60,0,0;  Start-Sleep -Milliseconds 800   # all red
Send 0x01,0,0,60;  Start-Sleep -Milliseconds 800   # all blue
Send 0x02,0,60,60,60; Start-Sleep -Milliseconds 800 # LED0 white (0x02 i R G B)
Send 0x00                                           # all off
$fs.Close()
Write-Host "Done."
