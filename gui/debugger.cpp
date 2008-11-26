
// ep128emu -- portable Enterprise 128 emulator
// Copyright (C) 2003-2007 Istvan Varga <istvanv@users.sourceforge.net>
// http://sourceforge.net/projects/ep128emu/
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

#include "ep128emu.hpp"
#include "gui.hpp"
#include "debuglib.hpp"
#include "debugger.hpp"

Ep128EmuGUI_LuaScript::~Ep128EmuGUI_LuaScript()
{
}

void Ep128EmuGUI_LuaScript::errorCallback(const char *msg)
{
  if (msg == (char *) 0 || msg[0] == '\0')
    msg = "Lua script error";
  debugWindow.gui.errorMessage(msg);
}

void Ep128EmuGUI_LuaScript::messageCallback(const char *msg)
{
  if (!msg)
    msg = "";
  debugWindow.monitor_->printMessage(msg);
}

Ep128EmuGUI_ScrollableOutput::~Ep128EmuGUI_ScrollableOutput()
{
}

int Ep128EmuGUI_ScrollableOutput::handle(int evt)
{
  if (evt == FL_MOUSEWHEEL) {
    int     tmp = Fl::event_dy();
    if (tmp > 0) {
      if (downWidget)
        downWidget->do_callback();
    }
    else if (tmp < 0) {
      if (upWidget)
        upWidget->do_callback();
    }
    return 1;
  }
  return Fl_Multiline_Output::handle(evt);
}

// ----------------------------------------------------------------------------

Ep128EmuGUI_DebugWindow::Ep128EmuGUI_DebugWindow(Ep128EmuGUI& gui_)
  : gui(gui_),
    luaScript(*this, gui_.vm)
{
  for (size_t i = 0; i < sizeof(windowTitle); i++)
    windowTitle[i] = '\0';
  std::strcpy(&(windowTitle[0]), "ep128emu debugger");
  savedWindowPositionX = 32;
  savedWindowPositionY = 32;
  focusWidget = (Fl_Widget *) 0;
  prvTab = (Fl_Widget *) 0;
  memoryDumpStartAddress = 0x00000000U;
  memoryDumpViewAddress = 0x00000000U;
  memoryDumpCPUAddressMode = true;
  ixViewOffset = 0;
  iyViewOffset = 0;
  disassemblyStartAddress = 0x00000000U;
  disassemblyViewAddress = 0x00000000U;
  disassemblyNextAddress = 0x00000000U;
  for (int i = 0; i < 6; i++)
    breakPointLists[i] = "";
  tmpBuffer.reserve(960);
  bpEditBuffer = new Fl_Text_Buffer();
  scriptEditBuffer = new Fl_Text_Buffer();
  createDebugWindow();
  window->label(&(windowTitle[0]));
  memoryDumpDisplay->upWidget = memoryDumpPrvPageButton;
  memoryDumpDisplay->downWidget = memoryDumpNxtPageButton;
  memoryDisplay_IX->upWidget = ixPrvPageButton;
  memoryDisplay_IX->downWidget = ixNxtPageButton;
  memoryDisplay_IY->upWidget = iyPrvPageButton;
  memoryDisplay_IY->downWidget = iyNxtPageButton;
  disassemblyDisplay->upWidget = disassemblyPrvPageButton;
  disassemblyDisplay->downWidget = disassemblyNxtPageButton;
}

Ep128EmuGUI_DebugWindow::~Ep128EmuGUI_DebugWindow()
{
  delete window;
  delete bpEditBuffer;
  delete scriptEditBuffer;
}

void Ep128EmuGUI_DebugWindow::show()
{
  monitor_->closeTraceFile();
  updateWindow();
  if (!window->shown()) {
    window->resize(savedWindowPositionX, savedWindowPositionY, 960, 720);
    if (focusWidget != (Fl_Widget *) 0 && focusWidget != monitor_)
      focusWidget->take_focus();
    else
      stepButton->take_focus();
  }
  window->show();
}

bool Ep128EmuGUI_DebugWindow::shown()
{
  return bool(window->shown());
}

void Ep128EmuGUI_DebugWindow::hide()
{
  if (window->shown()) {
    savedWindowPositionX = window->x();
    savedWindowPositionY = window->y();
  }
  window->hide();
  if (gui.debugWindowOpenFlag) {
    gui.debugWindowOpenFlag = false;
    gui.unlockVMThread();
  }
  std::strcpy(&(windowTitle[0]), "ep128emu debugger");
  window->label(&(windowTitle[0]));
}

bool Ep128EmuGUI_DebugWindow::breakPoint(int type, uint16_t addr, uint8_t value)
{
  if ((type == 0 || type == 3) && monitor_->getIsTraceOn()) {
    monitor_->writeTraceFile(addr);
    if (type == 3)
      return false;
  }
  switch (type) {
  case 0:
  case 3:
    try {
      gui.vm.disassembleInstruction(tmpBuffer, addr, true);
      if (tmpBuffer.length() > 21 && tmpBuffer.length() <= 40) {
        std::sprintf(&(windowTitle[0]), "Break at PC=%04X: %s",
                     (unsigned int) (addr & 0xFFFF), (tmpBuffer.c_str() + 21));
        tmpBuffer = "";
        break;
      }
    }
    catch (...) {
    }
    tmpBuffer = "";
  case 1:
    std::sprintf(&(windowTitle[0]),
                 "Break on reading %02X from memory address %04X",
                 (unsigned int) (value & 0xFF), (unsigned int) (addr & 0xFFFF));
    break;
  case 2:
    std::sprintf(&(windowTitle[0]),
                 "Break on writing %02X to memory address %04X",
                 (unsigned int) (value & 0xFF), (unsigned int) (addr & 0xFFFF));
    break;
  case 5:
    std::sprintf(&(windowTitle[0]),
                 "Break on reading %02X from I/O port %02X",
                 (unsigned int) (value & 0xFF), (unsigned int) (addr & 0xFF));
    break;
  case 6:
    std::sprintf(&(windowTitle[0]),
                 "Break on writing %02X to I/O port %02X",
                 (unsigned int) (value & 0xFF), (unsigned int) (addr & 0xFF));
    break;
  default:
    std::sprintf(&(windowTitle[0]), "Break");
  }
  window->label(&(windowTitle[0]));
  disassemblyViewAddress = uint32_t(gui.vm.getProgramCounter() & 0xFFFF);
  if (focusWidget == monitor_)
    monitor_->breakMessage(&(windowTitle[0]));
  return true;
}

void Ep128EmuGUI_DebugWindow::updateWindow()
{
  try {
    gui.vm.listCPURegisters(tmpBuffer);
    cpuRegisterDisplay->value(tmpBuffer.c_str());
    {
      char  tmpBuf[64];
      std::sprintf(&(tmpBuf[0]), "0000-3FFF: %02X\n4000-7FFF: %02X\n"
                                 "8000-BFFF: %02X\nC000-FFFF: %02X",
                   (unsigned int) gui.vm.getMemoryPage(0),
                   (unsigned int) gui.vm.getMemoryPage(1),
                   (unsigned int) gui.vm.getMemoryPage(2),
                   (unsigned int) gui.vm.getMemoryPage(3));
      memoryPagingDisplay->value(&(tmpBuf[0]));
    }
    uint32_t  tmp = gui.vm.getStackPointer();
    uint32_t  startAddr = (tmp + 0xFFF4U) & 0xFFF8U;
    uint32_t  endAddr = (startAddr + 0x002FU) & 0xFFFFU;
    dumpMemory(tmpBuffer, startAddr, endAddr, tmp, true, true);
    stackMemoryDumpDisplay->value(tmpBuffer.c_str());
    tmpBuffer = "";
    updateMemoryDumpDisplay();
    updateIOPortDisplay();
    updateDisassemblyDisplay();
    bpPriorityThresholdValuator->value(
        double(gui.config.debug.bpPriorityThreshold));
  }
  catch (std::exception& e) {
    gui.errorMessage(e.what());
  }
}

void Ep128EmuGUI_DebugWindow::dumpMemory(std::string& buf,
                                         uint32_t startAddr, uint32_t endAddr,
                                         uint32_t cursorAddr, bool showCursor,
                                         bool isCPUAddress)
{
  try {
    char      tmpBuf[8];
    buf = "";
    int       cnt = 0;
    uint32_t  addrMask = uint32_t(isCPUAddress ? 0x0000FFFFU : 0x003FFFFFU);
    endAddr &= addrMask;
    cursorAddr &= addrMask;
    while (true) {
      startAddr &= addrMask;
      if (cnt == 8) {
        cnt = 0;
        buf += '\n';
      }
      if (!cnt) {
        if (isCPUAddress)
          std::sprintf(&(tmpBuf[0]), "  %04X", (unsigned int) startAddr);
        else
          std::sprintf(&(tmpBuf[0]), "%06X", (unsigned int) startAddr);
        buf += &(tmpBuf[0]);
      }
      if (!(cnt & 3)) {
        if (showCursor && startAddr == cursorAddr) {
          std::sprintf(&(tmpBuf[0]), "  *%02X",
                       (unsigned int) gui.vm.readMemory(startAddr,
                                                        isCPUAddress));
        }
        else {
          std::sprintf(&(tmpBuf[0]), "   %02X",
                       (unsigned int) gui.vm.readMemory(startAddr,
                                                        isCPUAddress));
        }
      }
      else {
        if (showCursor && startAddr == cursorAddr) {
          std::sprintf(&(tmpBuf[0]), " *%02X",
                       (unsigned int) gui.vm.readMemory(startAddr,
                                                        isCPUAddress));
        }
        else {
          std::sprintf(&(tmpBuf[0]), "  %02X",
                       (unsigned int) gui.vm.readMemory(startAddr,
                                                        isCPUAddress));
        }
      }
      buf += &(tmpBuf[0]);
      if (startAddr == endAddr)
        break;
      startAddr++;
      cnt++;
    }
  }
  catch (std::exception& e) {
    buf.clear();
    gui.errorMessage(e.what());
  }
}

void Ep128EmuGUI_DebugWindow::updateMemoryDumpDisplay()
{
  try {
    uint32_t  addrMask =
        uint32_t(memoryDumpCPUAddressMode ? 0x00FFFFU : 0x3FFFFFU);
    memoryDumpStartAddress &= addrMask;
    memoryDumpViewAddress &= addrMask;
    const char  *fmt = (memoryDumpCPUAddressMode ? "%04X" : "%06X");
    char  tmpBuf[64];
    std::sprintf(&(tmpBuf[0]), fmt, (unsigned int) memoryDumpStartAddress);
    memoryDumpStartAddressValuator->value(&(tmpBuf[0]));
    dumpMemory(tmpBuffer, memoryDumpViewAddress, memoryDumpViewAddress + 0x2FU,
               0U, false, memoryDumpCPUAddressMode);
    memoryDumpDisplay->value(tmpBuffer.c_str());
    for (int i = 0; i < 3; i++) {
      char      *bufp = &(tmpBuf[0]);
      int       n = 0;
      uint16_t  addr = 0x0000;
      const Ep128::Z80_REGISTERS& r = reinterpret_cast<const Ep128::Ep128VM *>(
                                          &(gui.vm))->getZ80Registers();
      switch (i) {
      case 0:
        addr = uint16_t(r.BC.W);
        n = std::sprintf(bufp, "(BC-04)");
        break;
      case 1:
        addr = uint16_t(r.DE.W);
        n = std::sprintf(bufp, "(DE-04)");
        break;
      case 2:
        addr = uint16_t(r.HL.W);
        n = std::sprintf(bufp, "(HL-04)");
        break;
      }
      bufp = bufp + n;
      for (int j = -4; j < 7; j++) {
        uint8_t tmp =
            gui.vm.readMemory(uint16_t((int(addr) + j) & 0xFFFF), true);
        if (j == 0)
          fmt = "  *%02X";
        else if (j == 4)
          fmt = "   %02X";
        else
          fmt = "  %02X";
        n = std::sprintf(bufp, fmt, (unsigned int) tmp & 0xFFU);
        bufp = bufp + n;
      }
      if (i != 2)
        *(bufp++) = '\n';
      *(bufp++) = '\0';
      if (i == 0)
        tmpBuffer = &(tmpBuf[0]);
      else
        tmpBuffer += &(tmpBuf[0]);
    }
    memoryDisplay_BC_DE_HL->value(tmpBuffer.c_str());
    for (int i = 0; i < 3; i++) {
      char      *bufp = &(tmpBuf[0]);
      int       n = 0;
      const Ep128::Z80_REGISTERS& r = reinterpret_cast<const Ep128::Ep128VM *>(
                                          &(gui.vm))->getZ80Registers();
      uint16_t  addr = uint16_t(r.IX.W);
      int32_t   offs = ixViewOffset + int32_t((i - 1) * 8);
      if (offs >= 0)
        n = std::sprintf(bufp, "(IX+%02X)", (unsigned int) offs & 0xFFU);
      else
        n = std::sprintf(bufp, "(IX-%02X)", (unsigned int) (-offs) & 0xFFU);
      bufp = bufp + n;
      addr = uint16_t((int32_t(addr) + offs) & 0xFFFF);
      for (int j = 0; j < 8; j++) {
        uint8_t tmp =
            gui.vm.readMemory(uint16_t((int(addr) + j) & 0xFFFF), true);
        if (j == 4)
          *(bufp++) = ' ';
        n = std::sprintf(bufp, "  %02X", (unsigned int) tmp & 0xFFU);
        bufp = bufp + n;
      }
      if (i != 2)
        *(bufp++) = '\n';
      *(bufp++) = '\0';
      if (i == 0)
        tmpBuffer = &(tmpBuf[0]);
      else
        tmpBuffer += &(tmpBuf[0]);
    }
    memoryDisplay_IX->value(tmpBuffer.c_str());
    for (int i = 0; i < 3; i++) {
      char      *bufp = &(tmpBuf[0]);
      int       n = 0;
      const Ep128::Z80_REGISTERS& r = reinterpret_cast<const Ep128::Ep128VM *>(
                                          &(gui.vm))->getZ80Registers();
      uint16_t  addr = uint16_t(r.IY.W);
      int32_t   offs = iyViewOffset + int32_t((i - 1) * 8);
      if (offs >= 0)
        n = std::sprintf(bufp, "(IY+%02X)", (unsigned int) offs & 0xFFU);
      else
        n = std::sprintf(bufp, "(IY-%02X)", (unsigned int) (-offs) & 0xFFU);
      bufp = bufp + n;
      addr = uint16_t((int32_t(addr) + offs) & 0xFFFF);
      for (int j = 0; j < 8; j++) {
        uint8_t tmp =
            gui.vm.readMemory(uint16_t((int(addr) + j) & 0xFFFF), true);
        if (j == 4)
          *(bufp++) = ' ';
        n = std::sprintf(bufp, "  %02X", (unsigned int) tmp & 0xFFU);
        bufp = bufp + n;
      }
      if (i != 2)
        *(bufp++) = '\n';
      *(bufp++) = '\0';
      if (i == 0)
        tmpBuffer = &(tmpBuf[0]);
      else
        tmpBuffer += &(tmpBuf[0]);
    }
    memoryDisplay_IY->value(tmpBuffer.c_str());
    tmpBuffer = "";
  }
  catch (std::exception& e) {
    gui.errorMessage(e.what());
  }
}

void Ep128EmuGUI_DebugWindow::updateIOPortDisplay()
{
  try {
    gui.vm.listIORegisters(tmpBuffer);
    ioPortDisplay->value(tmpBuffer.c_str());
    tmpBuffer = "";
  }
  catch (std::exception& e) {
    gui.errorMessage(e.what());
  }
}

long Ep128EmuGUI_DebugWindow::parseHexNumber(uint32_t& value, const char *s)
{
  long  cnt = 0L;
  if (!s)
    return 0L;
  while (*s == ' ' || *s == '\t') {
    s++;
    cnt++;
  }
  if (*s == '\0')
    return 0L;
  if (*s == '\r' || *s == '\n')
    return (-(cnt + 1L));
  uint32_t  tmpVal = 0U;
  while (true) {
    char  c = *s;
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\0') {
      value = tmpVal;
      return cnt;
    }
    tmpVal = (tmpVal << 4) & 0xFFFFFFFFU;
    if (c >= '0' && c <= '9')
      tmpVal += uint32_t(c - '0');
    else if (c >= 'A' && c <= 'F')
      tmpVal += uint32_t((c - 'A') + 10);
    else if (c >= 'a' && c <= 'f')
      tmpVal += uint32_t((c - 'a') + 10);
    else {
      gui.errorMessage("invalid hexadecimal number format");
      return 0L;
    }
    s++;
    cnt++;
  }
  return 0L;
}

void Ep128EmuGUI_DebugWindow::parseMemoryDump(const char *s)
{
  uint32_t  addr = 0U;
  bool      haveAddress = false;
  try {
    while (true) {
      uint32_t  tmp = 0U;
      long      n = parseHexNumber(tmp, s);
      if (!n)           // end of string or error
        break;
      if (n < 0L) {     // end of line
        n = (-n);
        haveAddress = false;
        s = s + n;
      }
      else {
        s = s + n;
        if (!haveAddress) {
          addr = tmp & 0x3FFFFFU;
          haveAddress = true;
        }
        else {
          gui.vm.writeMemory(addr, uint8_t(tmp & 0xFFU),
                             memoryDumpCPUAddressMode);
          addr++;
        }
      }
    }
  }
  catch (std::exception& e) {
    gui.errorMessage(e.what());
  }
}

void Ep128EmuGUI_DebugWindow::updateDisassemblyDisplay()
{
  try {
    disassemblyStartAddress &= 0xFFFFU;
    disassemblyViewAddress &= 0xFFFFU;
    disassemblyNextAddress &= 0xFFFFU;
    char  tmpBuf[8];
    std::sprintf(&(tmpBuf[0]), "%04X", (unsigned int) disassemblyStartAddress);
    disassemblyStartAddressValuator->value(&(tmpBuf[0]));
    std::string tmp;
    tmp.reserve(48);
    tmpBuffer = "";
    uint32_t  addr = disassemblySearchBack(2);
    uint32_t  pcAddr = uint32_t(gui.vm.getProgramCounter()) & 0xFFFFU;
    for (int i = 0; i < 23; i++) {
      if (i == 22)
        disassemblyNextAddress = addr;
      uint32_t  nxtAddr = gui.vm.disassembleInstruction(tmp, addr, true, 0);
      while (addr != nxtAddr) {
        if (addr == pcAddr)
          tmp[1] = '*';
        addr = (addr + 1U) & 0xFFFFU;
      }
      tmpBuffer += tmp;
      if (i != 22)
        tmpBuffer += '\n';
    }
    disassemblyDisplay->value(tmpBuffer.c_str());
    tmpBuffer = "";
  }
  catch (std::exception& e) {
    gui.errorMessage(e.what());
  }
}

uint32_t Ep128EmuGUI_DebugWindow::disassemblySearchBack(int insnCnt)
{
  uint32_t    addrTable[256];
  if (insnCnt > 50)
    insnCnt = 50;
  for (uint32_t offs = 28U; true; offs--) {
    int       addrCnt = 0;
    uint32_t  addr = disassemblyViewAddress - (uint32_t(insnCnt * 4) + offs);
    bool      doneFlag = false;
    addr = addr & 0xFFFFU;
    addrTable[addrCnt++] = addr;
    do {
      uint32_t  nxtAddr =
          Ep128::Z80Disassembler::getNextInstructionAddr(gui.vm, addr, true);
      while (addr != nxtAddr) {
        if (addr == disassemblyViewAddress)
          doneFlag = true;
        addr = (addr + 1U) & 0xFFFFU;
      }
      addrTable[addrCnt++] = addr;
    } while (!doneFlag);
    for (int i = 0; i < addrCnt; i++) {
      if (addrTable[i] == disassemblyViewAddress ||
          (offs == 0U && i == (addrCnt - 1))) {
        if (i >= insnCnt)
          return addrTable[i - insnCnt];
      }
    }
  }
  return disassemblyViewAddress;
}

void Ep128EmuGUI_DebugWindow::applyBreakPointList()
{
  const char  *buf = (char *) 0;
  try {
    buf = bpEditBuffer->text();
    if (!buf)
      throw std::bad_alloc();
    std::string bpListText(buf);
    std::free(const_cast<char *>(buf));
    buf = (char *) 0;
    Ep128Emu::BreakPointList  bpList(bpListText);
    gui.vm.clearBreakPoints();
    gui.vm.setBreakPoints(bpList);
  }
  catch (std::exception& e) {
    if (buf)
      std::free(const_cast<char *>(buf));
    gui.errorMessage(e.what());
  }
}

void Ep128EmuGUI_DebugWindow::breakPointCallback(void *userData, int type,
                                                 uint16_t addr, uint8_t value)
{
  Ep128EmuGUI_DebugWindow&  debugWindow =
      *(reinterpret_cast<Ep128EmuGUI_DebugWindow *>(userData));
  if (!debugWindow.luaScript.runBreakPointCallback(type, addr, value)) {
    return;
  }
  Ep128EmuGUI&  gui_ = debugWindow.gui;
  Fl::lock();
  if (gui_.exitFlag || !gui_.mainWindow->shown()) {
    Fl::unlock();
    return;
  }
  if (!debugWindow.breakPoint(type, addr, value)) {
    Fl::unlock();
    return;             // do not show debugger window if tracing
  }
  if (!debugWindow.shown()) {
    gui_.debugWindowShowFlag = true;
    Fl::awake();
  }
  while (gui_.debugWindowShowFlag) {
    Fl::unlock();
    gui_.updateDisplay();
    Fl::lock();
  }
  while (true) {
    bool  tmp = debugWindow.shown();
    Fl::unlock();
    if (!tmp)
      break;
    gui_.updateDisplay();
    Fl::lock();
  }
}

