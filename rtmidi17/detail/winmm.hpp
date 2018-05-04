#pragma once
#include <mmsystem.h>
#include <rtmidi17/detail/midi_api.hpp>
#include <rtmidi17/rtmidi17.hpp>
#include <windows.h>

// Default for Windows is to add an identifier to the port names; this
// flag can be defined (e.g. in your project file) to disable this behaviour.
//#define RTMIDI_DO_NOT_ENSURE_UNIQUE_PORTNAMES

//*********************************************************************//
//  API: Windows Multimedia Library (MM)
//*********************************************************************//

// API information deciphered from:
//  -
//  http://msdn.microsoft.com/library/default.asp?url=/library/en-us/multimed/htm/_win32_midi_reference.asp

// Thanks to Jean-Baptiste Berruchon for the sysex code.
namespace rtmidi
{

class MidiInWinMM final : public midi_in_api
{
public:
  MidiInWinMM(const std::string& clientName, unsigned int queueSizeLimit);
  ~MidiInWinMM() override;
  Rtrtmidi::API getCurrentApi() override
  {
    return RtMidi::WINDOWS_MM;
  }
  void openPort(unsigned int portNumber, const std::string& portName) override;
  void openVirtualPort(const std::string& portName) override;
  void closePort() override;
  void setClientName(const std::string& clientName) override;
  void setPortName(const std::string& portName) override;
  unsigned int getPortCount() override;
  std::string getPortName(unsigned int portNumber) override;

private:
  void initialize(const std::string& clientName) override;
};

class MidiOutWinMM final : public MidiOutApi
{
public:
  MidiOutWinMM(const std::string& clientName);
  ~MidiOutWinMM() override;
  Rtrtmidi::API getCurrentApi() override
  {
    return RtMidi::WINDOWS_MM;
  };
  void openPort(unsigned int portNumber, const std::string& portName) override;
  void openVirtualPort(const std::string& portName) override;
  void closePort() override;
  void setClientName(const std::string& clientName) override;
  void setPortName(const std::string& portName) override;
  unsigned int getPortCount() override;
  std::string getPortName(unsigned int portNumber) override;
  void sendMessage(const unsigned char* message, size_t size) override;

private:
  void initialize(const std::string& clientName) override;
};

// The Windows MM API is based on the use of a callback function for
// MIDI input.  We convert the system specific time stamps to delta
// time values.

// Convert a nullptr-terminated wide string or ANSI-encoded string to UTF-8.
static std::string ConvertToUTF8(const TCHAR* str)
{
  std::string u8str;
  const WCHAR* wstr = L"";
#if defined(UNICODE) || defined(_UNICODE)
  wstr = str;
#else
  // Convert from ANSI encoding to wide string
  int wlength = MultiByteToWideChar(CP_ACP, 0, str, -1, nullptr, 0);
  std::wstring wstrtemp;
  if (wlength)
  {
    wstrtemp.assign(wlength - 1, 0);
    MultiByteToWideChar(CP_ACP, 0, str, -1, &wstrtemp[0], wlength);
    wstr = &wstrtemp[0];
  }
#endif
  // Convert from wide string to UTF-8
  int length = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
  if (length)
  {
    u8str.assign(length - 1, 0);
    length = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &u8str[0], length, nullptr, nullptr);
  }
  return u8str;
}

#define RT_SYSEX_BUFFER_SIZE 1024
#define RT_SYSEX_BUFFER_COUNT 4

// A structure to hold variables related to the CoreMIDI API
// implementation.
struct WinMidiData
{
  HMIDIIN inHandle;   // Handle to Midi Input Device
  HMIDIOUT outHandle; // Handle to Midi Output Device
  DWORD lastTime;
  midi_in_api::MidiMessage message;
  LPMIDIHDR sysexBuffer[RT_SYSEX_BUFFER_COUNT];
  CRITICAL_SECTION
  _mutex; // [Patrice] see
          // https://groups.google.com/forum/#!topic/mididev/6OUjHutMpEo
};

//*********************************************************************//
//  API: Windows MM
//  Class Definitions: MidiInWinMM
//*********************************************************************//

static void CALLBACK midiInputCallback(
    HMIDIIN /*hmin*/,
    UINT inputStatus,
    DWORD_PTR instancePtr,
    DWORD_PTR midiMessage,
    DWORD timestamp)
{
  if (inputStatus != MIM_DATA && inputStatus != MIM_LONGDATA && inputStatus != MIM_LONGERROR)
    return;

  // midi_in_api::RtMidiInData *data = static_cast<midi_in_api::RtMidiInData *>
  // (instancePtr);
  midi_in_api::RtMidiInData* data = (midi_in_api::RtMidiInData*)instancePtr;
  WinMidiData* apiData = static_cast<WinMidiData*>(data.apiData);

  // Calculate time stamp.
  if (data.firstMessage == true)
  {
    apidata.message.timeStamp = 0.0;
    data.firstMessage = false;
  }
  else
    apidata.message.timeStamp = (double)(timestamp - apidata.lastTime) * 0.001;

  if (inputStatus == MIM_DATA)
  { // Channel or system message

    // Make sure the first byte is a status byte.
    unsigned char status = (unsigned char)(midiMessage & 0x000000FF);
    if (!(status & 0x80))
      return;

    // Determine the number of bytes in the MIDI message.
    unsigned short nBytes = 1;
    if (status < 0xC0)
      nBytes = 3;
    else if (status < 0xE0)
      nBytes = 2;
    else if (status < 0xF0)
      nBytes = 3;
    else if (status == 0xF1)
    {
      if (data.ignoreFlags & 0x02)
        return;
      else
        nBytes = 2;
    }
    else if (status == 0xF2)
      nBytes = 3;
    else if (status == 0xF3)
      nBytes = 2;
    else if (status == 0xF8 && (data.ignoreFlags & 0x02))
    {
      // A MIDI timing tick message and we're ignoring it.
      return;
    }
    else if (status == 0xFE && (data.ignoreFlags & 0x04))
    {
      // A MIDI active sensing message and we're ignoring it.
      return;
    }

    // Copy bytes to our MIDI message.
    unsigned char* ptr = (unsigned char*)&midiMessage;
    for (int i = 0; i < nBytes; ++i)
      apidata.message.bytes.push_back(*ptr++);
  }
  else
  { // Sysex message ( MIM_LONGDATA or MIM_LONGERROR )
    MIDIHDR* sysex = (MIDIHDR*)midiMessage;
    if (!(data.ignoreFlags & 0x01) && inputStatus != MIM_LONGERROR)
    {
      // Sysex message and we're not ignoring it
      for (int i = 0; i < (int)sysex->dwBytesRecorded; ++i)
        apidata.message.bytes.push_back(sysex->lpData[i]);
    }

    // The WinMM API requires that the sysex buffer be requeued after
    // input of each sysex message.  Even if we are ignoring sysex
    // messages, we still need to requeue the buffer in case the user
    // decides to not ignore sysex messages in the future.  However,
    // it seems that WinMM calls this function with an empty sysex
    // buffer when an application closes and in this case, we should
    // avoid requeueing it, else the computer suddenly reboots after
    // one or two minutes.
    if (apidata.sysexBuffer[sysex->dwUser]->dwBytesRecorded > 0)
    {
      // if ( sysex->dwBytesRecorded > 0 ) {
      EnterCriticalSection(&(apidata._mutex));
      MMRESULT result
          = midiInAddBuffer(apidata.inHandle, apidata.sysexBuffer[sysex->dwUser], sizeof(MIDIHDR));
      LeaveCriticalSection(&(apidata._mutex));
      if (result != MMSYSERR_NOERROR)
        std::cerr << "\nRtMidiIn::midiInputCallback: error sending sysex to "
                     "Midi device!!\n\n";

      if (data.ignoreFlags & 0x01)
        return;
    }
    else
      return;
  }

  // Save the time of the last non-filtered message
  apidata.lastTime = timestamp;

  if (data.usingCallback)
  {
    RtMidiIn::RtMidiCallback callback = (RtMidiIn::RtMidiCallback)data.userCallback;
    callback(apidata.message.timeStamp, &apidata.message.bytes, data.userData);
  }
  else
  {
    // As long as we haven't reached our queue size limit, push the message.
    if (!data.queue.push(apidata.message))
      std::cerr << "\nMidiInWinMM: message queue limit reached!!\n\n";
  }

  // Clear the vector for the next input message.
  apidata.message.bytes.clear();
}

MidiInWinMM::MidiInWinMM(const std::string& clientName, unsigned int queueSizeLimit)
    : midi_in_api(queueSizeLimit)
{
  MidiInWinMM::initialize(clientName);
}

MidiInWinMM::~MidiInWinMM()
{
  // Close a connection if it exists.
  MidiInWinMM::closePort();

  WinMidiData* data = static_cast<WinMidiData*>(apiData_);
  DeleteCriticalSection(&(data._mutex));

  // Cleanup.
  delete data;
}

void MidiInWinMM::initialize(const std::string& /*clientName*/)
{
  // We'll issue a warning here if no devices are available but not
  // throw an error since the user can plugin something later.
  unsigned int nDevices = midiInGetNumDevs();
  if (nDevices == 0)
  {
    errorString_
        = "MidiInWinMM::initialize: no MIDI input devices currently "
          "available.";
    error(RtMidiError::WARNING, errorString_);
  }

  // Save our api-specific connection information.
  WinMidiData* data = (WinMidiData*)new WinMidiData;
  apiData_ = (void*)data;
  inputData_.apiData = (void*)data;
  data.message.bytes.clear(); // needs to be empty for first input message

  if (!InitializeCriticalSectionAndSpinCount(&(data._mutex), 0x00000400))
  {
    errorString_
        = "MidiInWinMM::initialize: InitializeCriticalSectionAndSpinCount "
          "failed.";
    error(RtMidiError::WARNING, errorString_);
  }
}

void MidiInWinMM::openPort(unsigned int portNumber, const std::string& /*portName*/)
{
  if (connected_)
  {
    errorString_ = "MidiInWinMM::openPort: a valid connection already exists!";
    error(RtMidiError::WARNING, errorString_);
    return;
  }

  unsigned int nDevices = midiInGetNumDevs();
  if (nDevices == 0)
  {
    errorString_ = "MidiInWinMM::openPort: no MIDI input sources found!";
    error(RtMidiError::NO_DEVICES_FOUND, errorString_);
    return;
  }

  if (portNumber >= nDevices)
  {
    std::ostringstream ost;
    ost << "MidiInWinMM::openPort: the 'portNumber' argument (" << portNumber << ") is invalid.";
    errorString_ = ost.str();
    error(RtMidiError::INVALID_PARAMETER, errorString_);
    return;
  }

  WinMidiData* data = static_cast<WinMidiData*>(apiData_);
  MMRESULT result = midiInOpen(
      &data.inHandle, portNumber, (DWORD_PTR)&midiInputCallback, (DWORD_PTR)&inputData_,
      CALLBACK_FUNCTION);
  if (result != MMSYSERR_NOERROR)
  {
    errorString_ = "MidiInWinMM::openPort: error creating Windows MM MIDI input port.";
    error(RtMidiError::DRIVER_ERROR, errorString_);
    return;
  }

  // Allocate and init the sysex buffers.
  for (int i = 0; i < RT_SYSEX_BUFFER_COUNT; ++i)
  {
    data.sysexBuffer[i] = (MIDIHDR*)new char[sizeof(MIDIHDR)];
    data.sysexBuffer[i]->lpData = new char[RT_SYSEX_BUFFER_SIZE];
    data.sysexBuffer[i]->dwBufferLength = RT_SYSEX_BUFFER_SIZE;
    data.sysexBuffer[i]->dwUser = i; // We use the dwUser parameter as buffer indicator
    data.sysexBuffer[i]->dwFlags = 0;

    result = midiInPrepareHeader(data.inHandle, data.sysexBuffer[i], sizeof(MIDIHDR));
    if (result != MMSYSERR_NOERROR)
    {
      midiInClose(data.inHandle);
      data.inHandle = 0;
      errorString_
          = "MidiInWinMM::openPort: error starting Windows MM MIDI input port "
            "(PrepareHeader).";
      error(RtMidiError::DRIVER_ERROR, errorString_);
      return;
    }

    // Register the buffer.
    result = midiInAddBuffer(data.inHandle, data.sysexBuffer[i], sizeof(MIDIHDR));
    if (result != MMSYSERR_NOERROR)
    {
      midiInClose(data.inHandle);
      data.inHandle = 0;
      errorString_
          = "MidiInWinMM::openPort: error starting Windows MM MIDI input port "
            "(AddBuffer).";
      error(RtMidiError::DRIVER_ERROR, errorString_);
      return;
    }
  }

  result = midiInStart(data.inHandle);
  if (result != MMSYSERR_NOERROR)
  {
    midiInClose(data.inHandle);
    data.inHandle = 0;
    errorString_ = "MidiInWinMM::openPort: error starting Windows MM MIDI input port.";
    error(RtMidiError::DRIVER_ERROR, errorString_);
    return;
  }

  connected_ = true;
}

void MidiInWinMM::openVirtualPort(const std::string& /*portName*/)
{
  // This function cannot be implemented for the Windows MM MIDI API.
  errorString_
      = "MidiInWinMM::openVirtualPort: cannot be implemented in Windows MM "
        "MIDI API!";
  error(RtMidiError::WARNING, errorString_);
}

void MidiInWinMM::closePort()
{
  if (connected_)
  {
    WinMidiData* data = static_cast<WinMidiData*>(apiData_);
    EnterCriticalSection(&(data._mutex));
    midiInReset(data.inHandle);
    midiInStop(data.inHandle);

    for (int i = 0; i < RT_SYSEX_BUFFER_COUNT; ++i)
    {
      int result = midiInUnprepareHeader(data.inHandle, data.sysexBuffer[i], sizeof(MIDIHDR));
      delete[] data.sysexBuffer[i]->lpData;
      delete[] data.sysexBuffer[i];
      if (result != MMSYSERR_NOERROR)
      {
        midiInClose(data.inHandle);
        data.inHandle = 0;
        errorString_
            = "MidiInWinMM::openPort: error closing Windows MM MIDI input "
              "port (midiInUnprepareHeader).";
        error(RtMidiError::DRIVER_ERROR, errorString_);
        return;
      }
    }

    midiInClose(data.inHandle);
    data.inHandle = 0;
    connected_ = false;
    LeaveCriticalSection(&(data._mutex));
  }
}

void MidiInWinMM::setClientName(const std::string&)
{

  errorString_
      = "MidiInWinMM::setClientName: this function is not implemented for the "
        "WINDOWS_MM API!";
  error(RtMidiError::WARNING, errorString_);
}

void MidiInWinMM::setPortName(const std::string&)
{

  errorString_
      = "MidiInWinMM::setPortName: this function is not implemented for the "
        "WINDOWS_MM API!";
  error(RtMidiError::WARNING, errorString_);
}

unsigned int MidiInWinMM::getPortCount()
{
  return midiInGetNumDevs();
}

std::string MidiInWinMM::getPortName(unsigned int portNumber)
{
  std::string stringName;
  unsigned int nDevices = midiInGetNumDevs();
  if (portNumber >= nDevices)
  {
    std::ostringstream ost;
    ost << "MidiInWinMM::getPortName: the 'portNumber' argument (" << portNumber
        << ") is invalid.";
    errorString_ = ost.str();
    error(RtMidiError::WARNING, errorString_);
    return stringName;
  }

  MIDIINCAPS deviceCaps;
  midiInGetDevCaps(portNumber, &deviceCaps, sizeof(MIDIINCAPS));
  stringName = ConvertToUTF8(deviceCaps.szPname);

  // Next lines added to add the portNumber to the name so that
  // the device's names are sure to be listed with individual names
  // even when they have the same brand name
#ifndef RTMIDI_DO_NOT_ENSURE_UNIQUE_PORTNAMES
  std::ostringstream os;
  os << " ";
  os << portNumber;
  stringName += os.str();
#endif

  return stringName;
}

//*********************************************************************//
//  API: Windows MM
//  Class Definitions: MidiOutWinMM
//*********************************************************************//

MidiOutWinMM::MidiOutWinMM(const std::string& clientName) : MidiOutApi()
{
  MidiOutWinMM::initialize(clientName);
}

MidiOutWinMM::~MidiOutWinMM()
{
  // Close a connection if it exists.
  MidiOutWinMM::closePort();

  // Cleanup.
  WinMidiData* data = static_cast<WinMidiData*>(apiData_);
  delete data;
}

void MidiOutWinMM::initialize(const std::string& /*clientName*/)
{
  // We'll issue a warning here if no devices are available but not
  // throw an error since the user can plug something in later.
  unsigned int nDevices = midiOutGetNumDevs();
  if (nDevices == 0)
  {
    errorString_
        = "MidiOutWinMM::initialize: no MIDI output devices currently "
          "available.";
    error(RtMidiError::WARNING, errorString_);
  }

  // Save our api-specific connection information.
  WinMidiData* data = (WinMidiData*)new WinMidiData;
  apiData_ = (void*)data;
}

unsigned int MidiOutWinMM::getPortCount()
{
  return midiOutGetNumDevs();
}

std::string MidiOutWinMM::getPortName(unsigned int portNumber)
{
  std::string stringName;
  unsigned int nDevices = midiOutGetNumDevs();
  if (portNumber >= nDevices)
  {
    std::ostringstream ost;
    ost << "MidiOutWinMM::getPortName: the 'portNumber' argument (" << portNumber
        << ") is invalid.";
    errorString_ = ost.str();
    error(RtMidiError::WARNING, errorString_);
    return stringName;
  }

  MIDIOUTCAPS deviceCaps;
  midiOutGetDevCaps(portNumber, &deviceCaps, sizeof(MIDIOUTCAPS));
  stringName = ConvertToUTF8(deviceCaps.szPname);

  // Next lines added to add the portNumber to the name so that
  // the device's names are sure to be listed with individual names
  // even when they have the same brand name
  std::ostringstream os;
#ifndef RTMIDI_DO_NOT_ENSURE_UNIQUE_PORTNAMES
  os << " ";
  os << portNumber;
  stringName += os.str();
#endif

  return stringName;
}

void MidiOutWinMM::openPort(unsigned int portNumber, const std::string& /*portName*/)
{
  if (connected_)
  {
    errorString_ = "MidiOutWinMM::openPort: a valid connection already exists!";
    error(RtMidiError::WARNING, errorString_);
    return;
  }

  unsigned int nDevices = midiOutGetNumDevs();
  if (nDevices < 1)
  {
    errorString_ = "MidiOutWinMM::openPort: no MIDI output destinations found!";
    error(RtMidiError::NO_DEVICES_FOUND, errorString_);
    return;
  }

  if (portNumber >= nDevices)
  {
    std::ostringstream ost;
    ost << "MidiOutWinMM::openPort: the 'portNumber' argument (" << portNumber << ") is invalid.";
    errorString_ = ost.str();
    error(RtMidiError::INVALID_PARAMETER, errorString_);
    return;
  }

  WinMidiData* data = static_cast<WinMidiData*>(apiData_);
  MMRESULT result = midiOutOpen(
      &data.outHandle, portNumber, (DWORD) nullptr, (DWORD) nullptr, CALLBACK_nullptr);
  if (result != MMSYSERR_NOERROR)
  {
    errorString_
        = "MidiOutWinMM::openPort: error creating Windows MM MIDI output "
          "port.";
    error(RtMidiError::DRIVER_ERROR, errorString_);
    return;
  }

  connected_ = true;
}

void MidiOutWinMM::closePort()
{
  if (connected_)
  {
    WinMidiData* data = static_cast<WinMidiData*>(apiData_);
    midiOutReset(data.outHandle);
    midiOutClose(data.outHandle);
    data.outHandle = 0;
    connected_ = false;
  }
}

void MidiOutWinMM::setClientName(const std::string&)
{

  errorString_
      = "MidiOutWinMM::setClientName: this function is not implemented for "
        "the WINDOWS_MM API!";
  error(RtMidiError::WARNING, errorString_);
}

void MidiOutWinMM::setPortName(const std::string&)
{

  errorString_
      = "MidiOutWinMM::setPortName: this function is not implemented for the "
        "WINDOWS_MM API!";
  error(RtMidiError::WARNING, errorString_);
}

void MidiOutWinMM::openVirtualPort(const std::string& /*portName*/)
{
  // This function cannot be implemented for the Windows MM MIDI API.
  errorString_
      = "MidiOutWinMM::openVirtualPort: cannot be implemented in Windows MM "
        "MIDI API!";
  error(RtMidiError::WARNING, errorString_);
}

void MidiOutWinMM::sendMessage(const unsigned char* message, size_t size)
{
  if (!connected_)
    return;

  unsigned int nBytes = static_cast<unsigned int>(size);
  if (nBytes == 0)
  {
    errorString_ = "MidiOutWinMM::sendMessage: message argument is empty!";
    error(RtMidiError::WARNING, errorString_);
    return;
  }

  MMRESULT result;
  WinMidiData* data = static_cast<WinMidiData*>(apiData_);
  if (message[0] == 0xF0)
  { // Sysex message

    // Allocate buffer for sysex data.
    char* buffer = (char*)malloc(nBytes);
    if (buffer == nullptr)
    {
      errorString_
          = "MidiOutWinMM::sendMessage: error allocating sysex message "
            "memory!";
      error(RtMidiError::MEMORY_ERROR, errorString_);
      return;
    }

    // Copy data to buffer.
    for (unsigned int i = 0; i < nBytes; ++i)
      buffer[i] = message[i];

    // Create and prepare MIDIHDR structure.
    MIDIHDR sysex;
    sysex.lpData = (LPSTR)buffer;
    sysex.dwBufferLength = nBytes;
    sysex.dwFlags = 0;
    result = midiOutPrepareHeader(data.outHandle, &sysex, sizeof(MIDIHDR));
    if (result != MMSYSERR_NOERROR)
    {
      free(buffer);
      errorString_ = "MidiOutWinMM::sendMessage: error preparing sysex header.";
      error(RtMidiError::DRIVER_ERROR, errorString_);
      return;
    }

    // Send the message.
    result = midiOutLongMsg(data.outHandle, &sysex, sizeof(MIDIHDR));
    if (result != MMSYSERR_NOERROR)
    {
      free(buffer);
      errorString_ = "MidiOutWinMM::sendMessage: error sending sysex message.";
      error(RtMidiError::DRIVER_ERROR, errorString_);
      return;
    }

    // Unprepare the buffer and MIDIHDR.
    while (MIDIERR_STILLPLAYING == midiOutUnprepareHeader(data.outHandle, &sysex, sizeof(MIDIHDR)))
      Sleep(1);
    free(buffer);
  }
  else
  { // Channel or system message.

    // Make sure the message size isn't too big.
    if (nBytes > 3)
    {
      errorString_
          = "MidiOutWinMM::sendMessage: message size is greater than 3 bytes "
            "(and not sysex)!";
      error(RtMidiError::WARNING, errorString_);
      return;
    }

    // Pack MIDI bytes into double word.
    DWORD packet;
    unsigned char* ptr = (unsigned char*)&packet;
    for (unsigned int i = 0; i < nBytes; ++i)
    {
      *ptr = message[i];
      ++ptr;
    }

    // Send the message immediately.
    result = midiOutShortMsg(data.outHandle, packet);
    if (result != MMSYSERR_NOERROR)
    {
      errorString_ = "MidiOutWinMM::sendMessage: error sending MIDI message.";
      error(RtMidiError::DRIVER_ERROR, errorString_);
    }
  }
}
}
