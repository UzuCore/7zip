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
#include "../Compress/DeflateEncoder.h"
#include "IArchive.h"
#include "../../../C/7zCrc.h"

#include "AlzEgg/third_party/unegg/nest/Events.h"
#include "AlzEgg/third_party/unegg/nest/Info.h"
#include "AlzEgg/third_party/unegg/nest/format/ALZFormat.h"
#include "AlzEgg/third_party/unegg/nest/format/EggFormat.h"
#include "AlzEgg/third_party/unegg/nest/stream/InputStream.h"
#include "AlzEgg/third_party/unegg/nest/stream/OutputStream.h"

#include <memory>
#include <stdio.h>
#include <vector>
#include <string.h>

namespace nest {
namespace utf8 {
size_t utf82euckr(const char *str, std::string *euckr);
}
}

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
  CHandlerEvents(): _owner(NULL) {}
  void SetOwner(CHandler *owner) { _owner = owner; }

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

Z7_CLASS_IMP_CHandler_IInArchive_1(IOutArchive)

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
  explicit CHandler(bool isEgg = false): _isEgg(isEgg), _isSolid(false), _testMode(false), _askMode(0),
      _itemCount(0), _callbackResult(S_OK), _selectedTotal(0)
  {
    _events.SetOwner(this);
  }

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
        // The ALZ parser converts EUC-KR names to UTF-8; EGG uses its own UTF-8 flag.
        bool isUtf8 = !_isEgg;
        if (_isEgg)
        {
          bool isUtf8Handled = false;
          isUtf8 = info->AsBool(nest::key::file::name::IS_UTF8, &isUtf8Handled) &&
              isUtf8Handled;
        }
        const UINT codePage = isUtf8 ? CP_UTF8 : 949;
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

namespace
{

struct CWriteBlock
{
  UInt32 Size;
  UInt32 CRC;
  UInt32 Method;
  UInt32 PackedSize;
  FILE *PackedData;

  CWriteBlock(): Size(0), CRC(0), Method(0), PackedSize(0), PackedData(NULL) {}
};

struct CWriteItem
{
  std::string Name;
  bool IsDir;
  UInt64 Size;
  UInt64 MTime;
  UInt32 DosTime;
  UInt32 CRC;
  UInt32 Method;
  UInt64 PackedSize;
  FILE *Data;
  FILE *PackedData;
  std::vector<CWriteBlock> Blocks;

  CWriteItem(): IsDir(false), Size(0), MTime(0), DosTime(0x00210000), CRC(0), Method(0),
      PackedSize(0), Data(NULL), PackedData(NULL) {}
};

class CTempFiles
{
  std::vector<FILE *> _files;
public:
  ~CTempFiles()
  {
    for (size_t i = 0; i < _files.size(); ++i)
      if (_files[i])
        fclose(_files[i]);
  }

  FILE *Create()
  {
    FILE *file = NULL;
#if defined(_MSC_VER)
    if (tmpfile_s(&file) != 0)
      return NULL;
#else
    file = tmpfile();
#endif
    if (file)
      _files.push_back(file);
    return file;
  }
};

Z7_CLASS_IMP_NOQIB_1(CTempFileInStream, ISequentialInStream)
  FILE *_file;
  UInt64 _remaining;
public:
  void Init(FILE *file, UInt64 size) { _file = file; _remaining = size; }
};

Z7_CLASS_IMP_NOQIB_1(CTempFileOutStream, ISequentialOutStream)
  FILE *_file;
  UInt64 _size;
public:
  void Init(FILE *file) { _file = file; _size = 0; }
  UInt64 GetSize() const { return _size; }
};

Z7_COM7F_IMF(CTempFileInStream::Read(void *data, UInt32 size, UInt32 *processedSize))
{
  *processedSize = 0;
  if (_remaining == 0 || size == 0)
    return S_OK;
  const size_t request = (_remaining > size) ? size : (size_t)_remaining;
  const size_t processed = fread(data, 1, request, _file);
  if (processed == 0 && ferror(_file))
    return E_FAIL;
  _remaining -= processed;
  *processedSize = (UInt32)processed;
  return S_OK;
}

Z7_COM7F_IMF(CTempFileOutStream::Write(const void *data, UInt32 size, UInt32 *processedSize))
{
  *processedSize = 0;
  if (size == 0)
    return S_OK;
  const size_t processed = fwrite(data, 1, size, _file);
  if (processed != size)
    return E_FAIL;
  _size += processed;
  *processedSize = (UInt32)processed;
  return S_OK;
}

static HRESULT WriteBytes(ISequentialOutStream *stream, const void *data, UInt32 size)
{
  return WriteStream(stream, data, size);
}

static HRESULT WriteByte(ISequentialOutStream *stream, Byte value)
{
  return WriteBytes(stream, &value, 1);
}

static HRESULT WriteUInt16(ISequentialOutStream *stream, UInt16 value)
{
  const Byte bytes[2] = { (Byte)value, (Byte)(value >> 8) };
  return WriteBytes(stream, bytes, sizeof(bytes));
}

static HRESULT WriteUInt32(ISequentialOutStream *stream, UInt32 value)
{
  const Byte bytes[4] =
  {
    (Byte)value, (Byte)(value >> 8), (Byte)(value >> 16), (Byte)(value >> 24)
  };
  return WriteBytes(stream, bytes, sizeof(bytes));
}

static HRESULT WriteUInt64(ISequentialOutStream *stream, UInt64 value)
{
  Byte bytes[8];
  for (unsigned i = 0; i < 8; ++i)
    bytes[i] = (Byte)(value >> (i * 8));
  return WriteBytes(stream, bytes, sizeof(bytes));
}

static HRESULT WriteTempBytes(FILE *file, UInt64 size, ISequentialOutStream *stream)
{
  Byte buffer[1 << 16];
  while (size != 0)
  {
    const size_t request = (size > sizeof(buffer)) ? sizeof(buffer) : (size_t)size;
    const size_t processed = fread(buffer, 1, request, file);
    if (processed == 0)
      return E_FAIL;
    RINOK(WriteBytes(stream, buffer, (UInt32)processed))
    size -= processed;
  }
  return S_OK;
}

static HRESULT CompressTemp(FILE *source, UInt64 sourceSize, CTempFiles &tempFiles,
    FILE **packedData, UInt64 *packedSize)
{
  FILE *packed = tempFiles.Create();
  if (!packed)
    return E_FAIL;

  CMyComPtr2_Create<ISequentialInStream, CTempFileInStream> input;
  input->Init(source, sourceSize);
  CMyComPtr2_Create<ISequentialOutStream, CTempFileOutStream> output;
  output->Init(packed);
  CMyComPtr2_Create<ICompressCoder, NCompress::NDeflate::NEncoder::CCOMCoder> encoder;
  RINOK(encoder.Interface()->Code(input, output, NULL, NULL, NULL))
  if (fflush(packed) != 0)
    return E_FAIL;
  *packedData = packed;
  *packedSize = output->GetSize();
  return S_OK;
}

static HRESULT ReadAndStageFile(IArchiveUpdateCallback *callback, UInt32 index,
    CWriteItem &item, CTempFiles &tempFiles, UInt64 &completed,
    UInt64 eggBlockSize)
{
  CMyComPtr<ISequentialInStream> input;
  RINOK(callback->GetStream(index, &input))
  if (!input)
    return item.Size == 0 ? S_OK : E_FAIL;

  FILE *data = tempFiles.Create();
  if (!data)
    return E_FAIL;
  item.Data = data;

  Byte buffer[1 << 16];
  UInt64 remaining = item.Size;
  UInt32 fileCRC = CRC_INIT_VAL;
  UInt32 blockCRC = CRC_INIT_VAL;
  UInt32 blockSize = 0;

  while (remaining != 0)
  {
    const UInt32 request = (UInt32)(remaining > sizeof(buffer) ? sizeof(buffer) : remaining);
    UInt32 processed = 0;
    RINOK(input->Read(buffer, request, &processed))
    if (processed == 0 || processed > request)
      return E_FAIL;
    if (fwrite(buffer, 1, processed, data) != processed)
      return E_FAIL;

    fileCRC = CrcUpdate(fileCRC, buffer, processed);
    UInt32 offset = 0;
    while (offset < processed)
    {
      const UInt32 room = (UInt32)(eggBlockSize - blockSize);
      const UInt32 part = (processed - offset < room) ? processed - offset : room;
      blockCRC = CrcUpdate(blockCRC, buffer + offset, part);
      blockSize += part;
      offset += part;
      if (blockSize == eggBlockSize)
      {
        CWriteBlock block;
        block.Size = blockSize;
        block.CRC = CRC_GET_DIGEST(blockCRC);
        item.Blocks.push_back(block);
        blockSize = 0;
        blockCRC = CRC_INIT_VAL;
      }
    }

    remaining -= processed;
    completed += processed;
    RINOK(callback->SetCompleted(&completed))
  }

  if (blockSize != 0)
  {
    CWriteBlock block;
    block.Size = blockSize;
    block.CRC = CRC_GET_DIGEST(blockCRC);
    item.Blocks.push_back(block);
  }
  item.CRC = CRC_GET_DIGEST(fileCRC);
  if (fflush(data) != 0)
    return E_FAIL;
  return S_OK;
}

static HRESULT WriteAlzArchive(ISequentialOutStream *stream,
    const std::vector<CWriteItem> &items, IArchiveUpdateCallback *callback)
{
  RINOK(WriteUInt32(stream, 0x015A4C41)) // ALZ signature
  RINOK(WriteUInt16(stream, 10))
  RINOK(WriteUInt16(stream, 0))

  for (UInt32 i = 0; i < items.size(); ++i)
  {
    const CWriteItem &item = items[i];
    RINOK(WriteUInt32(stream, 0x015A4C42)) // file header
    RINOK(WriteUInt16(stream, (UInt16)item.Name.size()))
    RINOK(WriteByte(stream, item.IsDir ? 0x10 : 0x20))
    RINOK(WriteUInt32(stream, item.DosTime))
    if (item.IsDir)
    {
      RINOK(WriteUInt16(stream, 0))
    }
    else
    {
      RINOK(WriteUInt16(stream, 0x80)) // 8-byte packed and unpacked sizes
      RINOK(WriteUInt16(stream, (UInt16)item.Method))
      RINOK(WriteUInt32(stream, item.CRC))
      RINOK(WriteUInt64(stream, item.Method == 2 ? item.PackedSize : item.Size))
      RINOK(WriteUInt64(stream, item.Size))
    }
    RINOK(WriteBytes(stream, item.Name.data(), (UInt32)item.Name.size()))
    if (!item.IsDir)
    {
      FILE *payload = item.Method == 2 ? item.PackedData : item.Data;
      if (payload)
        rewind(payload);
      RINOK(WriteTempBytes(payload,
          item.Method == 2 ? item.PackedSize : item.Size, stream))
    }
    RINOK(callback->SetOperationResult(NUpdate::NOperationResult::kOK))
  }

  RINOK(WriteUInt32(stream, 0x015A4C43)) // end marker
  RINOK(WriteUInt32(stream, 0))
  RINOK(WriteUInt32(stream, 0))
  RINOK(WriteUInt32(stream, 0))
  return S_OK;
}

static HRESULT WriteEggArchive(ISequentialOutStream *stream,
    const std::vector<CWriteItem> &items, IArchiveUpdateCallback *callback)
{
  static const UInt32 kEnd = 0x08E28222;
  static const UInt32 kBlock = 0x02B50C13;
  static const UInt32 kFile = 0x0A8590E3;
  static const UInt32 kFilename = 0x0A8591AC;
  static const UInt32 kWindowsInfo = 0x2C86950B;

  RINOK(WriteUInt32(stream, 0x41474745)) // EGGA
  RINOK(WriteUInt16(stream, 0x0100))
  RINOK(WriteUInt32(stream, 1))
  RINOK(WriteUInt32(stream, 0))
  RINOK(WriteUInt32(stream, kEnd)) // end of archive prefix

  for (UInt32 i = 0; i < items.size(); ++i)
  {
    const CWriteItem &item = items[i];
    RINOK(WriteUInt32(stream, kFile))
    RINOK(WriteUInt32(stream, i))
    RINOK(WriteUInt64(stream, item.Size))

    RINOK(WriteUInt32(stream, kFilename))
    RINOK(WriteByte(stream, 0)) // UTF-8 relative path
    RINOK(WriteUInt16(stream, (UInt16)item.Name.size()))
    RINOK(WriteBytes(stream, item.Name.data(), (UInt32)item.Name.size()))

    RINOK(WriteUInt32(stream, kWindowsInfo))
    RINOK(WriteByte(stream, 0))
    RINOK(WriteUInt16(stream, 9))
    RINOK(WriteUInt64(stream, item.MTime))
    RINOK(WriteByte(stream, item.IsDir ? 0x80 : 0x20))
    RINOK(WriteUInt32(stream, kEnd)) // end of file sub-headers

    if (!item.IsDir && item.Size != 0)
    {
      if (!item.Data)
        return E_FAIL;
      rewind(item.Data);
      for (size_t blockIndex = 0; blockIndex < item.Blocks.size(); ++blockIndex)
      {
        const CWriteBlock &block = item.Blocks[blockIndex];
        const UInt32 packedSize = block.Method == 1 ? block.PackedSize : block.Size;
        RINOK(WriteUInt32(stream, kBlock))
        RINOK(WriteByte(stream, (Byte)block.Method))
        RINOK(WriteByte(stream, 0))
        RINOK(WriteUInt32(stream, block.Size))
        RINOK(WriteUInt32(stream, packedSize))
        RINOK(WriteUInt32(stream, block.CRC))
        RINOK(WriteUInt32(stream, kEnd))
        FILE *payload = block.Method == 1 ? block.PackedData : item.Data;
        if (block.Method == 1 && payload)
          rewind(payload);
        RINOK(WriteTempBytes(payload, packedSize, stream))
      }
    }
    RINOK(callback->SetOperationResult(NUpdate::NOperationResult::kOK))
  }

  RINOK(WriteUInt32(stream, kEnd)) // end of archive
  return S_OK;
}

}

Z7_COM7F_IMF(CHandler::GetFileTimeType(UInt32 *timeType))
{
  if (!timeType)
    return E_INVALIDARG;
  *timeType = NFileTimeType::kWindows;
  return S_OK;
}

Z7_COM7F_IMF(CHandler::UpdateItems(ISequentialOutStream *outStream, UInt32 numItems,
    IArchiveUpdateCallback *updateCallback))
{
  COM_TRY_BEGIN
  if (!outStream || !updateCallback)
    return E_INVALIDARG;
  if (_format)
    return E_NOTIMPL; // updating an existing ALZ/EGG archive is not implemented

  std::vector<CWriteItem> items;
  items.reserve(numItems);
  UInt64 totalSize = 0;
  for (UInt32 i = 0; i < numItems; ++i)
  {
    Int32 newData = 0;
    Int32 newProps = 0;
    UInt32 indexInArchive = 0;
    RINOK(updateCallback->GetUpdateItemInfo(i, &newData, &newProps, &indexInArchive))
    (void)indexInArchive;
    if (!newProps)
      return E_NOTIMPL;

    CWriteItem item;
    {
      NCOM::CPropVariant prop;
      RINOK(updateCallback->GetProperty(i, kpidPath, &prop))
      if (prop.vt != VT_BSTR)
        return E_INVALIDARG;
      UString path = prop.bstrVal;
      AString utf8Name = UnicodeStringToMultiByte(path, CP_UTF8);
      if (utf8Name.IsEmpty())
        return E_INVALIDARG;
      if (_isEgg)
        item.Name.assign(utf8Name.Ptr(), utf8Name.Len());
      else if (nest::utf8::utf82euckr(utf8Name.Ptr(), &item.Name) != 0)
        return E_INVALIDARG;
      for (size_t j = 0; j < item.Name.size(); ++j)
        if (item.Name[j] == '\\')
          item.Name[j] = '/';
    }
    {
      NCOM::CPropVariant prop;
      RINOK(updateCallback->GetProperty(i, kpidIsDir, &prop))
      if (prop.vt == VT_BOOL)
        item.IsDir = prop.boolVal != VARIANT_FALSE;
      else if (prop.vt != VT_EMPTY)
        return E_INVALIDARG;
    }
    if (!newData && !item.IsDir)
      return E_NOTIMPL;
    if (item.IsDir && item.Name[item.Name.size() - 1] != '/')
      item.Name.push_back('/');
    if (item.Name.empty() || item.Name.size() > 0xFFFF)
      return E_INVALIDARG;

    if (!item.IsDir)
    {
      NCOM::CPropVariant prop;
      RINOK(updateCallback->GetProperty(i, kpidSize, &prop))
      if (prop.vt == VT_UI8)
        item.Size = prop.uhVal.QuadPart;
      else if (prop.vt == VT_UI4)
        item.Size = prop.ulVal;
      else
        return E_INVALIDARG;
      if (item.Size > (UInt64)(-1) - totalSize)
        return E_FAIL;
      totalSize += item.Size;
    }
    {
      NCOM::CPropVariant prop;
      RINOK(updateCallback->GetProperty(i, kpidMTime, &prop))
      if (prop.vt == VT_FILETIME)
      {
        if (_isEgg)
          item.MTime = ((UInt64)prop.filetime.dwHighDateTime << 32) | prop.filetime.dwLowDateTime;
        else
          NTime::FileTime_To_DosTime(prop.filetime, item.DosTime);
      }
      else if (prop.vt != VT_EMPTY)
        return E_INVALIDARG;
    }
    items.push_back(item);
  }

  RINOK(updateCallback->SetTotal(totalSize))
  UInt64 completed = 0;
  RINOK(updateCallback->SetCompleted(&completed))
  CrcGenerateTable();
  CTempFiles tempFiles;
  const UInt64 eggBlockSize = (UInt64)256 * 1024 * 1024;
  for (UInt32 i = 0; i < numItems; ++i)
  {
    if (!items[i].IsDir)
      RINOK(ReadAndStageFile(updateCallback, i, items[i], tempFiles, completed, eggBlockSize))
  }

  for (UInt32 i = 0; i < numItems; ++i)
  {
    CWriteItem &item = items[i];
    if (item.IsDir || item.Size == 0 || !item.Data)
      continue;
    rewind(item.Data);
    if (!_isEgg)
    {
      UInt64 packedSize = 0;
      FILE *packedData = NULL;
      RINOK(CompressTemp(item.Data, item.Size, tempFiles, &packedData, &packedSize))
      if (packedSize < item.Size)
      {
        item.Method = 2; // ALZ Deflate
        item.PackedData = packedData;
        item.PackedSize = packedSize;
      }
    }
    else
    {
      for (size_t j = 0; j < item.Blocks.size(); ++j)
      {
        CWriteBlock &block = item.Blocks[j];
        UInt64 packedSize = 0;
        FILE *packedData = NULL;
        RINOK(CompressTemp(item.Data, block.Size, tempFiles, &packedData, &packedSize))
        if (packedSize < block.Size)
        {
          block.Method = 1; // EGG Deflate
          block.PackedData = packedData;
          block.PackedSize = (UInt32)packedSize;
        }
      }
    }
  }

  if (_isEgg)
    return WriteEggArchive(outStream, items, updateCallback);
  return WriteAlzArchive(outStream, items, updateCallback);
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

static IInArchive *CreateAlzIn() { return new NAlzEgg::CHandler(false); }
static IOutArchive *CreateAlzOut() { return new NAlzEgg::CHandler(false); }

REGISTER_ARC_R("ALZ", "alz", NULL, 0xE6, Z7_ARRAY_SIZE(kSigAlz), kSigAlz,
    0, 0, 0, CreateAlzIn, CreateAlzOut, NULL)

}
namespace NEgg {

static IInArchive *CreateEggIn() { return new NAlzEgg::CHandler(true); }
static IOutArchive *CreateEggOut() { return new NAlzEgg::CHandler(true); }

REGISTER_ARC_R("EGG", "egg", NULL, 0xE8, Z7_ARRAY_SIZE(kSigEgg), kSigEgg,
    0, 0, 0, CreateEggIn, CreateEggOut, NULL)

}}
