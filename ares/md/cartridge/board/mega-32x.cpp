struct Mega32X : Interface {
  using Interface::Interface;
  std::unique_ptr<Board::Interface> board;

  auto load() -> void override {
    if(pak && pak->attribute("everdrive").boolean()) {
      board = std::make_unique<Board::EverDrivePro>(*cartridge);  //SSF ROM, or the EverDrive as RAM cart
    } else if(pak && pak->attribute("ramcart").boolean()) {
      board = std::make_unique<Board::RamCart>(*cartridge);
    } else if(pak) {
      board = std::make_unique<Board::Standard>(*cartridge);
    } else {
      board = std::make_unique<Board::Interface>(*cartridge);
    }

    board->pak = pak;
    board->load();

    cartridge->child = *board;
  }

  auto save() -> void override {
    board->save();
  }

  //a cart with a thread of its own (the EverDrive's neo68k core): its clock and its work
  auto frequency() -> u32 override { return board ? board->frequency() : Interface::frequency(); }
  auto main() -> void override { if(board) return board->main(); Interface::main(); }

  auto read(n1 upper, n1 lower, n24 address, n16 data) -> n16 override {
    if(!m32x.io.adapterEnable) {
      return board->read(upper, lower, address, data);
    }

    if(address >= 0x000000 && address <= 0x0000ff) {
      return m32x.readExternal(upper, lower, address, data);
    }

    if(m32x.dreq.vram) {
      return board->read(upper, lower, address, data);
    }

    return data;
  }

  auto write(n1 upper, n1 lower, n24 address, n16 data) -> void override {
    if(!m32x.io.adapterEnable) {
      return board->write(upper, lower, address, data);
    }

    if(address >= 0x000000 && address <= 0x0000ff) {
      return m32x.writeExternal(upper, lower, address, data);
    }

    if(m32x.dreq.vram) {
      //with the 32X on, the 68000 cannot write the first 512 KB of the cartridge (0x000100-0x07ffff)
      //directly: console, 25 Oct 2026 (Mega EverDrive PRO, CD32X BootCamp EDCD.32X) - nothing landed
      //there until the copy went through another window with that bank mapped in.  The other windows
      //(0x080000-0x3fffff) and the cartridge at 0x400000 (a Mega CD booted its disc) take writes.
      if(address <= 0x07ffff) return;
      return board->write(upper, lower, address, data);
    }
  }

  auto readIO(n1 upper, n1 lower, n24 address, n16 data) -> n16 override {
    data = board->readIO(upper, lower, address, data);

    return m32x.readExternalIO(upper, lower, address, data);
  }

  auto writeIO(n1 upper, n1 lower, n24 address, n16 data) -> void override {
    //with the 32X on, a write to the cartridge's registers (/TIME, 0xa130xx) reaches the cartridge
    //only while RV = 1.  Console, 25 Oct 2026 (Mega EverDrive PRO, CD32X BootCamp EDCD.32X): its
    //mapper writes made with RV = 0 were not taken (the cart stayed write-protected); with RV = 1,
    //as edtest and Chilly Willy's SRAM code make them, they are.
    bool cartRegister = address >= 0xa13000 && address <= 0xa130ff;
    if(getenv("ARES_EDIO_TRACE") && address >= 0xa130d0 && address <= 0xa130df) { static u32 n; if(n++ < 60) print("[32x] /TIME write ", hex(address, 6L), " rv ", (u32)m32x.dreq.vram, " ae ", (u32)m32x.io.adapterEnable, "\n"); }
    if(!cartRegister || !m32x.io.adapterEnable || m32x.dreq.vram) {
      board->writeIO(upper, lower, address, data);
    }

    return m32x.writeExternalIO(upper, lower, address, data);
  }

  auto vblank(bool line) -> void override {
    return m32x.vblank(line);
  }

  auto hblank(bool line) -> void override {
    return m32x.hblank(line);
  }

  auto power(bool reset) -> void override {
    board->power(reset);
  }

  auto serialize(serializer& s) -> void override {
    s(*board);
  }
};
