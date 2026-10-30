// Main.cpp

#include "StdAfx.h"
#include "SetupLocale.h"

#include "../../../../C/DllSecur.h"

#include "../../../Common/MyWindows.h"
#include "../../../Common/MyInitGuid.h"

#include "../../../Common/CommandLineParser.h"
#include "../../../Common/StringConvert.h"
#include "../../../Common/TextConfig.h"

#include "../../../Windows/DLL.h"
#include "../../../Windows/ErrorMsg.h"
#include "../../../Windows/FileDir.h"
#include "../../../Windows/FileFind.h"
#include "../../../Windows/FileIO.h"
#include "../../../Windows/FileName.h"
#include "../../../Windows/NtCheck.h"
#include "../../../Windows/ResourceString.h"

#include "../../UI/Explorer/MyMessages.h"

#include "ExtractEngine.h"

#include "resource.h"

using namespace NWindows;
using namespace NFile;
using namespace NDir;

extern
HINSTANCE g_hInstance;
HINSTANCE g_hInstance;
extern
bool g_DisableUserQuestions;
bool g_DisableUserQuestions;

static CFSTR const kTempDirPrefix = FTEXT("7zS");

#define MY_SHELL_EXECUTE

static const char kStartID[] = { ',','!','@','I','n','s','t','a','l','l','@','!','U','T','F','-','8','!' };
static const char kEndID[]   = { ',','!','@','I','n','s','t','a','l','l','E','n','d','@','!' };

static bool ReadDataString(CFSTR fileName, AString &stringResult)
{
  // stringResult.Empty(); // (stringResult) is empty already
  NIO::CInFile inFile;
  if (!inFile.Open(fileName))
    return false;
  const size_t kBufferSize = 1 << 12;

  Byte buffer[kBufferSize];
  const size_t startSize1 = sizeof(kStartID) - 1;
  const size_t endSize1 = sizeof(kEndID) - 1;
  
  size_t numBytesPrev = 0;
  bool scanMode = true;
  UInt32 posTotal = 0;
  for (;;)
  {
    const size_t numReadBytes = kBufferSize - numBytesPrev;
    size_t processedSize;
    if (!inFile.ReadFull(buffer + numBytesPrev, numReadBytes, processedSize))
      return false;
    if (processedSize == 0)
      break;
    numBytesPrev += processedSize;
    size_t pos = 0;
    for (;;)
    {
      if (!scanMode)
      {
        if (pos + endSize1 >= numBytesPrev)
          break;
        const Byte b = buffer[pos++];
        if (b == 0)
          return false;
        if (b == ';' && memcmp(buffer + pos, kEndID + 1, endSize1) == 0)
          return true;
        stringResult += (char)b;
      }
      else
      {
        if (pos + startSize1 >= numBytesPrev)
          break;
        const Byte b = buffer[pos++];
        if (b == ';' && memcmp(buffer + pos, kStartID + 1, startSize1) == 0)
        {
          scanMode = false;
          pos += startSize1;
        }
      }
    }
    posTotal += (UInt32)pos;
    if (posTotal > (1 << 21))
      break;
    numBytesPrev -= pos;
    memmove(buffer, buffer + pos, numBytesPrev);
  }
  return scanMode;
}

#if defined(_WIN32) && defined(_UNICODE) && !defined(_WIN64) && !defined(UNDER_CE)
#define NT_CHECK_FAIL_ACTION ShowErrorMessage(SetupText(L"Unsupported Windows version", L"\uc9c0\uc6d0\ud558\uc9c0 \uc54a\ub294 Windows \ubc84\uc804\uc785\ub2c8\ub2e4.")); return 1;
#endif

static void ShowErrorMessageSpec(const UString &name)
{
  UString message = NError::MyFormatMessage(::GetLastError());
  const int pos = message.Find(L"%1");
  if (pos >= 0)
  {
    message.Delete((unsigned)pos, 2);
    message.Insert((unsigned)pos, name);
  }
  ShowErrorMessage(NULL, message);
}

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE /* hPrevInstance */,
    #ifdef UNDER_CE
    LPWSTR
    #else
    LPSTR
    #endif
    /* lpCmdLine */,int /* nCmdShow */)
{
  g_hInstance = (HINSTANCE)hInstance;

  SetThreadUILanguage(IsKoreanSetup() ? MAKELANGID(LANG_KOREAN, SUBLANG_KOREAN) : MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US));

  NT_CHECK

  #ifdef _WIN32
  LoadSecurityDlls();
  #endif

  // InitCommonControls();

  UString archiveName, switches;
  #ifdef MY_SHELL_EXECUTE
  UString executeFile, executeParameters;
  #endif
  NCommandLineParser::SplitCommandLine(GetCommandLineW(), archiveName, switches);

  FString fullPath;
  NDLL::MyGetModuleFileName(fullPath);

  switches.Trim();
  bool assumeYes = false;
  if (switches.IsPrefixedBy_Ascii_NoCase("-y"))
  {
    assumeYes = true;
    switches = switches.Ptr(2);
    switches.Trim();
  }

  AString config;
  if (!ReadDataString(fullPath, config))
  {
    if (!assumeYes)
      ShowErrorMessage(SetupText(L"Can't load config info", L"\uc124\uce58 \uc124\uc815\uc744 \ubd88\ub7ec\uc62c \uc218 \uc5c6\uc2b5\ub2c8\ub2e4."));
    return 1;
  }

  UString dirPrefix ("." STRING_PATH_SEPARATOR);
  UString appLaunched;
  bool showProgress = true;
  if (!config.IsEmpty())
  {
    CObjectVector<CTextConfigPair> pairs;
    if (!GetTextConfig(config, pairs))
    {
      if (!assumeYes)
        ShowErrorMessage(SetupText(L"Config failed", L"\uc124\uce58 \uc124\uc815\uc774 \uc62c\ubc14\ub974\uc9c0 \uc54a\uc2b5\ub2c8\ub2e4."));
      return 1;
    }
    const UString friendlyName = GetTextConfigValue(pairs, "Title");
    const UString installPrompt = GetTextConfigValue(pairs, "BeginPrompt");
    const UString progress = GetTextConfigValue(pairs, "Progress");
    if (progress.IsEqualTo_Ascii_NoCase("no"))
      showProgress = false;
    const int index = FindTextConfigItem(pairs, "Directory");
    if (index >= 0)
      dirPrefix = pairs[index].String;
    if (!installPrompt.IsEmpty() && !assumeYes)
    {
      if (MessageBoxW(NULL, installPrompt, friendlyName, MB_YESNO |
          MB_ICONQUESTION) != IDYES)
        return 0;
    }
    appLaunched = GetTextConfigValue(pairs, "RunProgram");
    
    #ifdef MY_SHELL_EXECUTE
    executeFile = GetTextConfigValue(pairs, "ExecuteFile");
    executeParameters = GetTextConfigValue(pairs, "ExecuteParameters");
    if (IsKoreanSetup())
    {
      const UString koreanParameters = GetTextConfigValue(pairs, "ExecuteParameters_ko");
      if (!koreanParameters.IsEmpty())
        executeParameters = koreanParameters;
    }
    #endif
  }

  CTempDir tempDir;
  if (!tempDir.Create(kTempDirPrefix))
  {
    if (!assumeYes)
      ShowErrorMessage(SetupText(L"Cannot create temp folder archive", L"\uc124\uce58\uc6a9 \uc784\uc2dc \ud3f4\ub354\ub97c \ub9cc\ub4e4 \uc218 \uc5c6\uc2b5\ub2c8\ub2e4."));
    return 1;
  }

  CCodecs *codecs = new CCodecs;
  CMyComPtr<IUnknown> compressCodecsInfo = codecs;
  {
    const HRESULT result = codecs->Load();
    if (result != S_OK)
    {
      ShowErrorMessage(SetupText(L"Cannot load codecs", L"\uc555\ucd95 \ucf54\ub371\uc744 \ubd88\ub7ec\uc62c \uc218 \uc5c6\uc2b5\ub2c8\ub2e4."));
      return 1;
    }
  }

  const FString tempDirPath = tempDir.GetPath();
  // tempDirPath = "M:\\1\\"; // to test low disk space
  {
    bool isCorrupt = false;
    UString errorMessage;
    HRESULT result = ExtractArchive(codecs, fullPath, tempDirPath, showProgress,
      isCorrupt, errorMessage);
    
    if (result != S_OK)
    {
      if (!assumeYes)
      {
        if (result == S_FALSE || isCorrupt)
        {
          NWindows::MyLoadString(IDS_EXTRACTION_ERROR_MESSAGE, errorMessage);
          result = E_FAIL;
        }
        if (result != E_ABORT)
        {
          if (errorMessage.IsEmpty())
            errorMessage = NError::MyFormatMessage(result);
          ::MessageBoxW(NULL, errorMessage, NWindows::MyLoadString(IDS_EXTRACTION_ERROR_TITLE), MB_ICONERROR);
        }
      }
      return 1;
    }
  }

  #ifndef UNDER_CE
  CCurrentDirRestorer currentDirRestorer;
  if (!SetCurrentDir(tempDirPath))
    return 1;
  #endif
  
  HANDLE hProcess = NULL;
#ifdef MY_SHELL_EXECUTE
  if (!executeFile.IsEmpty())
  {
    CSysString filePath (GetSystemString(executeFile));
    SHELLEXECUTEINFO execInfo;
    execInfo.cbSize = sizeof(execInfo);
    execInfo.fMask = SEE_MASK_NOCLOSEPROCESS
      #ifndef UNDER_CE
      | SEE_MASK_FLAG_DDEWAIT
      #endif
      ;
    execInfo.hwnd = NULL;
    execInfo.lpVerb = NULL;
    execInfo.lpFile = filePath;

    if (!switches.IsEmpty())
    {
      executeParameters.Add_Space_if_NotEmpty();
      executeParameters += switches;
    }

    // Keep a diagnostic log even when launched by an older update checker.
    if (executeFile.IsEqualTo_Ascii_NoCase("msiexec.exe") &&
        switches.Find(L"/L") < 0 && switches.Find(L"/l") < 0)
    {
      wchar_t tempPath[MAX_PATH];
      const DWORD length = GetTempPathW(MAX_PATH, tempPath);
      if (length > 0 && length < MAX_PATH)
      {
        executeParameters += L" /L*V \"";
        executeParameters += tempPath;
        executeParameters += L"7zip-alpha-setup.log\"";
      }
    }

    const CSysString parametersSys (GetSystemString(executeParameters));
    if (parametersSys.IsEmpty())
      execInfo.lpParameters = NULL;
    else
      execInfo.lpParameters = parametersSys;

    execInfo.lpDirectory = NULL;
    execInfo.nShow = SW_SHOWNORMAL;
    execInfo.hProcess = NULL;
    /* BOOL success = */ ::ShellExecuteEx(&execInfo);
    UINT32 result = (UINT32)(UINT_PTR)execInfo.hInstApp;
    if (result <= 32)
    {
      if (!assumeYes)
        ShowErrorMessage(SetupText(L"Cannot open file", L"\uc124\uce58 \ud30c\uc77c\uc744 \uc5f4 \uc218 \uc5c6\uc2b5\ub2c8\ub2e4."));
      return 1;
    }
    hProcess = execInfo.hProcess;
  }
  else
#endif
  {
    if (appLaunched.IsEmpty())
    {
      appLaunched = "setup.exe";
      if (!NFind::DoesFileExist_FollowLink(us2fs(appLaunched)))
      {
        if (!assumeYes)
          ShowErrorMessage(SetupText(L"Cannot find setup.exe", L"\uc124\uce58 \ud504\ub85c\uadf8\ub7a8\uc744 \ucc3e\uc744 \uc218 \uc5c6\uc2b5\ub2c8\ub2e4."));
        return 1;
      }
    }
    
    {
      FString s2 = tempDirPath;
      NName::NormalizeDirPathPrefix(s2);
      appLaunched.Replace(L"%%T" WSTRING_PATH_SEPARATOR, fs2us(s2));
    }
    
    const UString appNameForError = appLaunched; // actually we need to rtemove parameters also

    appLaunched.Replace(L"%%T", fs2us(tempDirPath));

    if (!switches.IsEmpty())
    {
      appLaunched.Add_Space();
      appLaunched += switches;
    }
    STARTUPINFO startupInfo;
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.lpReserved = NULL;
    startupInfo.lpDesktop = NULL;
    startupInfo.lpTitle = NULL;
    startupInfo.dwFlags = 0;
    startupInfo.cbReserved2 = 0;
    startupInfo.lpReserved2 = NULL;
    
    PROCESS_INFORMATION processInformation;
    
    const CSysString appLaunchedSys (GetSystemString(dirPrefix + appLaunched));
    
    const BOOL createResult = CreateProcess(NULL,
        appLaunchedSys.Ptr_non_const(),
        NULL, NULL, FALSE, 0, NULL, NULL /*tempDir.GetPath() */,
        &startupInfo, &processInformation);
    if (createResult == 0)
    {
      if (!assumeYes)
      {
        // we print name of exe file, if error message is
        // ERROR_BAD_EXE_FORMAT: "%1 is not a valid Win32 application".
        ShowErrorMessageSpec(appNameForError);
      }
      return 1;
    }
    ::CloseHandle(processInformation.hThread);
    hProcess = processInformation.hProcess;
  }
  if (hProcess)
  {
    WaitForSingleObject(hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(hProcess, &exitCode);
    ::CloseHandle(hProcess);
    return (int)exitCode;
  }
  return 0;
}
