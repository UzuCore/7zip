// ALZ and EGG archive handlers backed by the UnEGG decoder.
#include "StdAfx.h"
#ifdef FILE_END
#undef FILE_END
#endif
#include "../IPassword.h"

#include "../../Common/ComTry.h"
#include "../../Common/StringConvert.h"
#include "../../Windows/PropVariant.h"
#include "../../Windows/TimeUtils.h"
#include "../Common/ProgressUtils.h"
#include "../Common/RegisterArc.h"
#include "../Common/StreamUtils.h"
#include "IArchive.h"

#include "AlzEgg/third_party/unegg/nest/Events.h"
#include "AlzEgg/third_party/unegg/nest/Info.h"
#include "AlzEgg/third_party/unegg/nest/format/ALZFormat.h"
#include "AlzEgg/third_party/unegg/nest/format/EggFormat.h"
#include "AlzEgg/third_party/unegg/nest/stream/InputStream.h"
#include "AlzEgg/third_party/unegg/nest/stream/OutputStream.h"

#include <memory>
#include <vector>
#include <string.h>

using namespace NWindows;

namespace NArchive {
namespace NAlzEgg {

static const Byte kProps[] =
{
  kpidPath,
  kpidIsDir,
  kpidSize,
  kpidPackSize,
  kpidMTime,
  kpidAttrib,
  kpidCRC,
  kpidEncrypted,
  kpidMethod
};

static const Byte kArcProps[] =
{
  kpidSolid,
  kpidComment
};

class CHandler;

class CUnEggInputStream: public nest::InputStream
{
  CMyComPtr<IInStream> _stream;

public:
  CUnEggInputStream(IInStream *stream, const char *virtualName): _stream(stream)
  {
    Initialize(virtualName ? virtualName : "archive");
  }

  int Open() override { return Result::Success; }
  void Close() override {}

  uint64 Seek(uint64 pos, int origin) override
  {
    if (!_stream)
      return (uint64)(Int64)-1;

    Int64 offset = (Int64)pos;
    DWORD seekOrigin = STREAM_SEEK_SET;
    if (origin == SEEK_CUR)
      seekOrigin = STREAM_SEEK_CUR;
    else if (origin == SEEK_END)
    {
      seekOrigin = STREAM_SEEK_END;
      offset = -(Int64)pos;
    }

    UInt64 newPosition = 0;
    if (_stream->Seek(offset, seekOrigin, &newPosition) != S_OK)
      return (uint64)(Int64)-1;
    return newPosition;
  }

  uint64 Tell() override
  {
    if (!_stream)
      return 0;
    UInt64 position = 0;
    if (_stream->Seek(0, STREAM_SEEK_CUR, &position) != S_OK)
      return 0;
    return position;
  }

  size_t Read(void *buffer, size_t size) override
  {
    if (!_stream || size == 0)
      return 0;

    const UInt32 request = (UInt32)(size > 0xFFFFFFFFu ? 0xFFFFFFFFu : size);
    UInt32 processed = 0;
    if (_stream->Read(buffer, request, &processed) != S_OK)
      return 0;
    return processed;
  }

  uint64 Size() override
  {
    if (!_stream)
      return 0;

    UInt64 oldPosition = 0;
    UInt64 size = 0;
    if (_stream->Seek(0, STREAM_SEEK_CUR, &oldPosition) != S_OK)
      return 0;
    if (_stream->Seek(0, STREAM_SEEK_END, &size) != S_OK)
      return 0;
    _stream->Seek((Int64)oldPosition, STREAM_SEEK_SET, NULL);
    return size;
  }
};

class CUnEggOutputStream: public nest::OutputStream
{
  CMyComPtr<ISequentialOutStream> _stream;
  bool _discard;

public:
  CUnEggOutputStream(ISequentialOutStream *stream, bool discard):
      _stream(stream), _discard(discard) {}

  int Create(const char *) override { return Result::Success; }
  int CreateFolder(const char *) override { return Result::Success; }
  void Close() override { _stream.Release(); }

  size_t Write(void *buffer, size_t size) override
  {
    if (_discard)
      return size;

    size_t total = 0;
    while (total < size)
    {
      const UInt32 chunk = (UInt32)((size - total) > 0xFFFFFFFFu ? 0xFFFFFFFFu : (size - total));
      UInt32 processed = 0;
      if (_stream->Write((Byte *)buffer + total, chunk, &processed) != S_OK || processed == 0)
        break;
      total += processed;
    }
    return total;
  }
};

class CHandlerEvents: public nest::Events
{
  CHandler *_owner;

public:
  explicit CHandlerEvents(CHandler *owner): _owner(owner) {}

  int Initialize(nest::Workspace *) override;
  void Finalize(int) override;
  int OnWorkStart(int) override;
  void OnWorkFinish(int, int) override;
  int OnFileStart(size_t) override;
  void OnFileFinish(size_t, int) override;
  int OnError(size_t, int errorCode) override;
  int OnQueryInputStream(size_t, nest::InputStream **stream, const char *virtualName, size_t queryCount) override;
  int OnQueryOutputStream(size_t, nest::OutputStream **stream, const char *virtualName) override;
  int OnQueryPassword(size_t, char *buffer, size_t size, size_t queryCount) override;
  int OnProgress(size_t index, int current, int max) override;
};

Z7_CLASS_IMP_CHandler_IInArchive_0

  friend class CHandlerEvents;

  std::unique_ptr<nest::Format> _format;
  std::unique_ptr<CUnEggInputStream> _input;
  std::vector<std::unique_ptr<CUnEggInputStream> > _volumeStreams;
  std::vector<std::unique_ptr<CUnEggOutputStream> > _outputStreams;
  CMyComPtr<IArchiveOpenVolumeCallback> _volumeCallback;
  CMyComPtr<IArchiveExtractCallback> _extractCallback;
  CHandlerEvents _events;
  bool _isEgg;
  bool _isSolid;
  bool _testMode;
  UInt32 _askMode;
  UInt32 _itemCount;
  HRESULT _callbackResult;
  std::vector<bool> _selected;
  std::vector<bool> _prepared;
  std::vector<UInt64> _itemSizes;
  std::vector<UInt64> _progressOffsets;
  UInt64 _selectedTotal;

  nest::Info *GetItemInfo(UInt32 index) const
  {
    if (!_format)
      return NULL;
    bool handled = false;
    nest::Info *info = _format->GetInfo()->AsChildInfo(nest::key::file::OBJECT, index, &handled);
    return handled ? info : NULL;
  }

  bool IsSelected(size_t index) const
  {
    return index < _selected.size() && _selected[index];
  }

  bool IsDirectory(UInt32 index) const
  {
    nest::Info *info = GetItemInfo(index);
    if (!info)
      return false;
    bool handled = false;
    const UInt32 attr = info->AsUInt32(nest::key::file::attribute::ATTRIBUTES, &handled);
    if (!handled)
      return false;
    return _isEgg ? ((attr & 0x80) != 0 || (attr & 0x40000) != 0)
                  : ((attr & 0x10) != 0 || (attr & 0x4000) != 0);
  }

  void ReportProgress(size_t index, int current, int max)
  {
    if (!_extractCallback || index >= _progressOffsets.size() || !IsSelected(index))
      return;
    UInt64 completed = _progressOffsets[index];
    if (max > 0 && current > 0)
    {
      const UInt64 divisor = (UInt64)max;
      const UInt64 progress = current > max ? divisor : (UInt64)current;
      const UInt64 size = _itemSizes[index];
      completed += (size / divisor) * progress + ((size % divisor) * progress) / divisor;
    }
    if (completed > _selectedTotal)
      completed = _selectedTotal;
    const HRESULT hr = _extractCallback->SetCompleted(&completed);
    if (hr != S_OK && _callbackResult == S_OK)
      _callbackResult = hr;
  }

public:
  CHandler(): _events(this), _isEgg(false), _isSolid(false), _testMode(false), _askMode(0),
      _itemCount(0), _callbackResult(S_OK), _selectedTotal(0) {}

  ~CHandler()
  {
    Close();
  }

};

IMP_IInArchive_Props
IMP_IInArchive_ArcProps

static UInt32 MapOperationResult(int result)
{
  switch (result)
  {
    case Result::Success:
      return NExtract::NOperationResult::kOK;
    case Result::InvalidCRC:
      return NExtract::NOperationResult::kCRCError;
    case Result::DecryptError:
      return NExtract::NOperationResult::kWrongPassword;
    default:
      return NExtract::NOperationResult::kDataError;
  }
}

HRESULT CHandler::Close() throw()
{
  if (_format)
  {
    _format->Close();
    _format.reset();
  }
  _input.reset();
  _volumeStreams.clear();
  _outputStreams.clear();
  _volumeCallback.Release();
  _extractCallback.Release();
  _itemCount = 0;
  _isSolid = false;
  _selected.clear();
  _prepared.clear();
  _itemSizes.clear();
  _progressOffsets.clear();
  _selectedTotal = 0;
  _callbackResult = S_OK;
  return S_OK;
}

HRESULT CHandler::Open(IInStream *stream, const UInt64 *, IArchiveOpenCallback *callback) throw()
{
  COM_TRY_BEGIN
  RINOK(Close())
  if (!stream)
    return E_INVALIDARG;

  Byte signature[4] = {};
  RINOK(InStream_SeekToBegin(stream))
  UInt32 processed = 0;
  const HRESULT readResult = stream->Read(signature, sizeof(signature), &processed);
  RINOK(InStream_SeekToBegin(stream))
  if (readResult != S_OK || processed != sizeof(signature))
    return S_FALSE;

  if (signature[0] == 'A' && signature[1] == 'L' && signature[2] == 'Z' && signature[3] == 1)
    _isEgg = false;
  else if (signature[0] == 'E' && signature[1] == 'G' && signature[2] == 'G' && signature[3] == 'A')
    _isEgg = true;
  else
    return S_FALSE;

  UString virtualName;
  if (callback)
  {
    callback->QueryInterface(IID_IArchiveOpenVolumeCallback, (void **)&_volumeCallback);
    if (_volumeCallback)
    {
      NCOM::CPropVariant nameProp;
      if (_volumeCallback->GetProperty(kpidName, &nameProp) == S_OK && nameProp.vt == VT_BSTR)
        virtualName = nameProp.bstrVal;
    }
  }
  if (virtualName.IsEmpty())
    virtualName = _isEgg ? L"archive.egg" : L"archive.alz";

  const AString nameUtf8 = UnicodeStringToMultiByte(virtualName, CP_UTF8);
  _input.reset(new CUnEggInputStream(stream, nameUtf8.Ptr()));
  if (_isEgg)
    _format.reset(new nest::EggFormat(&_events));
  else
    _format.reset(new nest::ALZFormat(&_events));

  int result = _format->Open(_input.get());
  if (result == Result::Success)
    result = _format->Scan();
  if (result != Result::Success)
  {
    Close();
    return S_FALSE;
  }

  bool solidHandled = false;
  _isSolid = _format->GetInfo()->AsBool(nest::key::archive::IS_SOLID, &solidHandled);
  if (!solidHandled)
    _isSolid = false;

  bool handled = false;
  const size_t count = _format->GetInfo()->AsSize(nest::key::archive::FILE_COUNT, &handled);
  if (!handled || count > 0xFFFFFFFFu)
  {
    Close();
    return S_FALSE;
  }
  _itemCount = (UInt32)count;
  _itemSizes.resize(_itemCount);
  for (UInt32 i = 0; i < _itemCount; ++i)
  {
    nest::Info *info = GetItemInfo(i);
    if (!info)
      continue;
    handled = false;
    _itemSizes[i] = info->AsUInt64(nest::key::file::size::UNPACKED, &handled);
    if (!handled)
      _itemSizes[i] = 0;
  }
  return S_OK;
  COM_TRY_END
}

HRESULT CHandler::GetNumberOfItems(UInt32 *numItems) throw()
{
  *numItems = _itemCount;
  return S_OK;
}

HRESULT CHandler::GetProperty(UInt32 index, PROPID propID, PROPVARIANT *value) throw()
{
  COM_TRY_BEGIN
  if (index >= _itemCount)
    return E_INVALIDARG;

  NCOM::CPropVariant prop;
  nest::Info *info = GetItemInfo(index);
  if (!info)
    return E_FAIL;

  bool handled = false;
  switch (propID)
  {
    case kpidPath:
    {
      const char *name = info->AsString(nest::key::file::name::NAME, &handled);
      if (handled && name)
      {
        bool isUtf8Handled = false;
        const bool isUtf8 = info->AsBool(nest::key::file::name::IS_UTF8, &isUtf8Handled);
        const UINT codePage = (isUtf8Handled && isUtf8) ? CP_UTF8 : 949;
        UString path = MultiByteToUnicodeString(name, codePage);
        path.Replace(L'/', WCHAR_PATH_SEPARATOR);
        path.Replace(L'\\', WCHAR_PATH_SEPARATOR);
        prop = path;
      }
      break;
    }
    case kpidIsDir:
      prop = IsDirectory(index);
      break;
    case kpidSize:
      prop = _itemSizes[index];
      break;
    case kpidPackSize:
    {
      const UInt64 size = info->AsUInt64(nest::key::file::size::PACKED, &handled);
      if (handled)
        prop = size;
      break;
    }
    case kpidMTime:
    {
      if (_isEgg)
      {
        const Int64 ticks = info->AsInt64(nest::key::file::attribute::MODIFIED, &handled);
        if (handled)
        {
          FILETIME ft;
          ft.dwLowDateTime = (DWORD)(UInt64)ticks;
          ft.dwHighDateTime = (DWORD)((UInt64)ticks >> 32);
          PropVariant_SetFrom_NtfsTime(prop, ft);
        }
      }
      else
      {
        const UInt32 dosTime = info->AsUInt32(nest::key::file::attribute::MODIFIED, &handled);
        if (handled)
          PropVariant_SetFrom_DosTime(prop, dosTime);
      }
      break;
    }
    case kpidAttrib:
    {
      const UInt32 attr = info->AsUInt32(nest::key::file::attribute::ATTRIBUTES, &handled);
      if (handled)
        prop = attr;
      break;
    }
    case kpidCRC:
    {
      const UInt32 crc = info->AsUInt32(nest::key::file::CRC, &handled);
      if (handled)
        prop = crc;
      break;
    }
    case kpidEncrypted:
    {
      const bool encrypted = info->AsBool(nest::key::file::IS_ENCRYPTED, &handled);
      if (handled)
        prop = encrypted;
      break;
    }
    case kpidMethod:
    {
      const char *method = info->AsString(nest::key::file::method::NAME, &handled);
      if (handled && method)
        prop = MultiByteToUnicodeString(method, CP_UTF8);
      break;
    }
    default:
      break;
  }
  prop.Detach(value);
  return S_OK;
  COM_TRY_END
}

HRESULT CHandler::GetArchiveProperty(PROPID propID, PROPVARIANT *value) throw()
{
  NCOM::CPropVariant prop;
  if (_format)
  {
    bool handled = false;
    switch (propID)
    {
      case kpidSolid:
      {
        const bool solid = _format->GetInfo()->AsBool(nest::key::archive::IS_SOLID, &handled);
        if (handled)
          prop = solid;
        break;
      }
      case kpidComment:
      {
        const char *comment = _format->GetInfo()->AsString(nest::key::archive::comment::COMMENT, &handled);
        if (handled && comment)
          prop = MultiByteToUnicodeString(comment, _isEgg ? CP_UTF8 : 949);
        break;
      }
      default:
        break;
    }
  }
  prop.Detach(value);
  return S_OK;
}

HRESULT CHandler::Extract(const UInt32 *indices, UInt32 numItems, Int32 testMode,
    IArchiveExtractCallback *extractCallback) throw()
{
  COM_TRY_BEGIN
  if (!_format || !extractCallback)
    return E_FAIL;
  if (numItems == 0)
    return S_OK;

  _selected.assign(_itemCount, false);
  _prepared.assign(_itemCount, false);
  _progressOffsets.assign(_itemCount, 0);
  _selectedTotal = 0;
  _testMode = (testMode != 0);
  _askMode = _testMode ? NExtract::NAskMode::kTest : NExtract::NAskMode::kExtract;
  _callbackResult = S_OK;
  _outputStreams.clear();

  if (numItems == (UInt32)(Int32)-1)
  {
    for (UInt32 i = 0; i < _itemCount; ++i)
      _selected[i] = true;
  }
  else
  {
    if (!indices)
      return E_INVALIDARG;
    for (UInt32 i = 0; i < numItems; ++i)
    {
      if (indices[i] >= _itemCount)
        return E_INVALIDARG;
      _selected[indices[i]] = true;
    }
  }

  _extractCallback = extractCallback;
  for (UInt32 i = 0; i < _itemCount; ++i)
  {
    _progressOffsets[i] = _selectedTotal;
    if (_selected[i])
    {
      if (_itemSizes[i] > (UInt64)(-1) - _selectedTotal)
      {
        _extractCallback.Release();
        return E_FAIL;
      }
      _selectedTotal += _itemSizes[i];
    }
  }

  const HRESULT totalResult = _extractCallback->SetTotal(_selectedTotal);
  if (totalResult != S_OK)
  {
    _extractCallback.Release();
    return totalResult;
  }

  const int result = _format->Extract();
  _outputStreams.clear();
  _extractCallback.Release();
  if (_callbackResult != S_OK)
    return _callbackResult;
  if (result == Result::Success || result == Result::Skip)
    return S_OK;
  return S_FALSE;
  COM_TRY_END
}

int CHandlerEvents::Initialize(nest::Workspace *) { return Result::Success; }
void CHandlerEvents::Finalize(int) {}
int CHandlerEvents::OnWorkStart(int) { return Result::Success; }
void CHandlerEvents::OnWorkFinish(int, int) {}

int CHandlerEvents::OnFileStart(size_t) { return Result::Success; }

void CHandlerEvents::OnFileFinish(size_t index, int result)
{
  if (!_owner->IsSelected(index) || index >= _owner->_prepared.size() || !_owner->_prepared[index])
    return;

  const HRESULT hr = _owner->_extractCallback->SetOperationResult(MapOperationResult(result));
  _owner->_prepared[index] = false;
  if (hr != S_OK && _owner->_callbackResult == S_OK)
    _owner->_callbackResult = hr;
}

int CHandlerEvents::OnError(size_t, int errorCode)
{
  return errorCode;
}

int CHandlerEvents::OnQueryInputStream(size_t, nest::InputStream **stream,
    const char *virtualName, size_t)
{
  *stream = NULL;
  if (!_owner->_volumeCallback)
    return Result::End;

  const UString name = MultiByteToUnicodeString(virtualName, CP_UTF8);
  CMyComPtr<IInStream> input;
  const HRESULT hr = _owner->_volumeCallback->GetStream(name, &input);
  if (hr == S_FALSE || !input)
    return Result::End;
  if (hr != S_OK)
  {
    _owner->_callbackResult = hr;
    return Result::Fail;
  }

  const AString nameUtf8 = UnicodeStringToMultiByte(name, CP_UTF8);
  std::unique_ptr<CUnEggInputStream> streamObject(
      new CUnEggInputStream(input, nameUtf8.Ptr()));
  *stream = streamObject.get();
  _owner->_volumeStreams.push_back(std::move(streamObject));
  return Result::Success;
}

int CHandlerEvents::OnQueryOutputStream(size_t index, nest::OutputStream **stream,
    const char *)
{
  *stream = NULL;
  if (!_owner->IsSelected(index))
    return Result::Skip;

  CMyComPtr<ISequentialOutStream> output;
  HRESULT hr = _owner->_extractCallback->GetStream((UInt32)index, &output, _owner->_askMode);
  if (hr != S_OK)
  {
    _owner->_callbackResult = hr;
    return Result::Fail;
  }

  if (!output && !_owner->_testMode && !_owner->IsDirectory((UInt32)index))
    return Result::Skip;

  hr = _owner->_extractCallback->PrepareOperation(_owner->_askMode);
  if (hr != S_OK)
  {
    _owner->_callbackResult = hr;
    return Result::Fail;
  }

  const bool discard = _owner->_testMode || !output;
  std::unique_ptr<CUnEggOutputStream> streamObject(
      new CUnEggOutputStream(output, discard));
  *stream = streamObject.get();
  const bool backendDeletesStream =
      _owner->_isEgg && _owner->_isSolid && !_owner->IsDirectory((UInt32)index);
  if (backendDeletesStream)
    streamObject.release();
  else
    _owner->_outputStreams.push_back(std::move(streamObject));
  _owner->_prepared[index] = true;
  return Result::Success;
}

int CHandlerEvents::OnQueryPassword(size_t, char *buffer, size_t size, size_t)
{
  if (!_owner->_extractCallback || size == 0)
    return Result::Fail;

  CMyComPtr<ICryptoGetTextPassword> passwordCallback;
  if (_owner->_extractCallback->QueryInterface(IID_ICryptoGetTextPassword,
      (void **)&passwordCallback) != S_OK || !passwordCallback)
    return Result::Fail;

  CMyComBSTR password;
  const HRESULT hr = passwordCallback->CryptoGetTextPassword(&password);
  if (hr != S_OK)
  {
    _owner->_callbackResult = hr;
    return Result::Fail;
  }

  const UString passwordString((const wchar_t *)password);
  const AString encoded = UnicodeStringToMultiByte(passwordString,
      _owner->_isEgg ? CP_UTF8 : 949);
  if (encoded.Len() + 1 > size)
    return Result::Fail;
  memcpy(buffer, encoded.Ptr(), encoded.Len());
  buffer[encoded.Len()] = 0;
  return Result::Success;
}

int CHandlerEvents::OnProgress(size_t index, int current, int max)
{
  _owner->ReportProgress(index, current, max);
  return _owner->_callbackResult == S_OK ? Result::Success : Result::Fail;
}

}}

static const Byte kSigAlz[] = { 'A', 'L', 'Z', 1 };
static const Byte kSigEgg[] = { 'E', 'G', 'G', 'A' };

namespace NArchive {
namespace NAlz {

REGISTER_ARC_I_CLS(NAlzEgg::CHandler, "ALZ", "alz", NULL, 0xE6,
    kSigAlz, 0, 0, NULL)

}
namespace NEgg {

REGISTER_ARC_I_CLS(NAlzEgg::CHandler, "EGG", "egg", NULL, 0xE8,
    kSigEgg, 0, 0, NULL)

}}
