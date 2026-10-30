# Verify the Authenticode file digest without changing certificate trust stores.
# A pinned self-signed publisher can legitimately terminate in an untrusted root.
if (-not ('SevenZipAlpha.UpdateTrust' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
namespace SevenZipAlpha {
    public static class UpdateTrust {
        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        struct FileInfo {
            public uint Size;
            [MarshalAs(UnmanagedType.LPWStr)] public string Path;
            public IntPtr Handle, Subject;
        }
        [StructLayout(LayoutKind.Sequential)]
        struct TrustData {
            public uint Size;
            public IntPtr Policy, Sip;
            public uint Ui, Revocation, Choice;
            public IntPtr File;
            public uint StateAction;
            public IntPtr State, Url;
            public uint Flags, UiContext;
            public IntPtr SignatureSettings;
        }
        [DllImport("wintrust.dll", ExactSpelling = true)]
        static extern int WinVerifyTrust(IntPtr window, ref Guid action, ref TrustData data);
        public static int Verify(string path) {
            FileInfo file = new FileInfo { Size = (uint)Marshal.SizeOf(typeof(FileInfo)), Path = path };
            IntPtr memory = Marshal.AllocHGlobal(Marshal.SizeOf(typeof(FileInfo)));
            Marshal.StructureToPtr(file, memory, false);
            TrustData data = new TrustData {
                Size = (uint)Marshal.SizeOf(typeof(TrustData)), Ui = 2, Choice = 1,
                File = memory, StateAction = 1, Flags = 0x1010
            };
            Guid action = new Guid("00AAC56B-CD44-11d0-8CC2-00C04FC295EE");
            try { return WinVerifyTrust(new IntPtr(-1), ref action, ref data); }
            finally {
                data.StateAction = 2;
                WinVerifyTrust(new IntPtr(-1), ref action, ref data);
                Marshal.DestroyStructure(memory, typeof(FileInfo));
                Marshal.FreeHGlobal(memory);
            }
        }
    }
}
'@
}
function Assert-UpdateInstaller {
    param(
        [Parameter(Mandatory=$true)][string]$Path,
        [Parameter(Mandatory=$true)][string]$Sha256,
        [Parameter(Mandatory=$true)][string]$PublisherThumbprint
    )
    if ($Sha256 -notmatch '^[a-fA-F0-9]{64}$' -or $PublisherThumbprint -notmatch '^[a-fA-F0-9]{40}$') {
        throw 'Invalid update identity.'
    }
    if ((Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -ine $Sha256) {
        throw 'The downloaded installer failed its integrity check.'
    }
    $signature = Get-AuthenticodeSignature -LiteralPath $Path
    if ($null -eq $signature.SignerCertificate -or $signature.SignerCertificate.Thumbprint -ine $PublisherThumbprint) {
        throw 'The installer signature could not be verified.'
    }
    $trust = [SevenZipAlpha.UpdateTrust]::Verify((Resolve-Path -LiteralPath $Path).Path)
    if ($trust -notin @(0, -2146762487)) { # CERT_E_UNTRUSTEDROOT is the only allowed trust exception.
        throw "The installer signature is invalid (trust result $trust)."
    }
}
