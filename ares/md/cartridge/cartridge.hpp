struct Cartridge;
#include "board/board.hpp"

struct Cartridge : Thread {
  Node::Peripheral node;
  VFS::Pak pak;

  auto title() const -> string { return information.title; }
  auto regions() const -> std::vector<string> { return information.regions; }
  //CART_IN line. A board can release it while the console runs (EverDrive PRO: CTRL0's C bit),
  //which turns a cartridge boot into a Mega CD boot (mode 1 to mode 2).
  auto bootable() const -> boolean { return information.bootable && !cartOff; }

  //cartridge.cpp
  auto allocate(Node::Port) -> Node::Peripheral;
  auto connect() -> void;
  auto disconnect() -> void;

  auto save() -> void;
  auto main() -> void;
  auto step(u32 clocks) -> void;
  auto power(bool reset) -> void;

  auto read(n1 upper, n1 lower, n24 address, n16 data) -> n16;
  auto write(n1 upper, n1 lower, n24 address, n16 data) -> void;

  auto readIO(n1 upper, n1 lower, n24 address, n16 data) -> n16;
  auto writeIO(n1 upper, n1 lower, n24 address, n16 data) -> void;

  auto vblank(bool line) -> void;
  auto hblank(bool line) -> void;

  //serialization.cpp
  auto serialize(serializer&) -> void;

  struct Information {
    string title;
    std::vector<string> regions;
    boolean bootable;
  } information;

  std::unique_ptr<Board::Interface> board;
  maybe<Board::Interface&> child;
  n1 cartOff;  //set by the board: CART_IN released
};

#include "slot.hpp"
extern Cartridge& cartridge;
