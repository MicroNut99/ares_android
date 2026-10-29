//mega-cd-32x.cpp: the RAM cart / Mega EverDrive PRO from Settings > Options
auto makeRamCartPak(u32 slot, const std::vector<u8>& launcher, string& cartFile) -> std::shared_ptr<vfs::directory>;
auto saveRamCartPak(std::shared_ptr<vfs::directory> ramCart, const string& cartFile) -> void;

struct Mega32X : Emulator {
  Mega32X();
  auto load() -> LoadResult override;
  auto unload() -> void override;
  auto save() -> bool override;
  auto pak(ares::Node::Object) -> std::shared_ptr<vfs::directory> override;

  std::shared_ptr<mia::Pak> disc;
  u32 regionID = 0;
  std::shared_ptr<vfs::directory> ramCart;  //the EverDrive PRO holding the ROM (a SEGA SSF ROM with the Mega CD)
  string ramCartFile;
};

Mega32X::Mega32X() {
  manufacturer = "Sega";
  name = "Mega 32X";

  for(auto id : range(2)) {
    InputPort port{string{"Controller Port ", 1 + id}};

  { InputDevice device{"Fighting Pad"};
    device.digital("Up",    virtualPorts[id].pad.up);
    device.digital("Down",  virtualPorts[id].pad.down);
    device.digital("Left",  virtualPorts[id].pad.left);
    device.digital("Right", virtualPorts[id].pad.right);
    device.digital("A",     virtualPorts[id].pad.west);
    device.digital("B",     virtualPorts[id].pad.south);
    device.digital("C",     virtualPorts[id].pad.east);
    device.digital("X",     virtualPorts[id].pad.l_bumper);
    device.digital("Y",     virtualPorts[id].pad.north);
    device.digital("Z",     virtualPorts[id].pad.r_bumper);
    device.digital("Mode",  virtualPorts[id].pad.select);
    device.digital("Start", virtualPorts[id].pad.start);
    port.append(device); }

  { InputDevice device{"Mega Mouse"};
    device.relative("X",      virtualPorts[id].mouse.x);
    device.relative("Y",      virtualPorts[id].mouse.y);
    device.digital ("Left",   virtualPorts[id].mouse.left);
    device.digital ("Right",  virtualPorts[id].mouse.right);
    device.digital ("Middle", virtualPorts[id].mouse.middle);
    device.digital ("Start",  virtualPorts[id].mouse.extra);
    port.append(device); }

    ports.push_back(port);
  }
}

auto Mega32X::load() -> LoadResult {
  game = mia::Medium::create("Mega 32X");
  string location = Emulator::load(game, configuration.game);
  if(!location) return noFileSelected;
  LoadResult result = game->load(location);
  if(result != successful) return result;
  
  auto region = Emulator::region();
  //if statements below are ordered by lowest to highest priority
  if(region == "PAL"   ) regionID = 2;
  if(region == "NTSC-J") regionID = 1;
  if(region == "NTSC-U") regionID = 0;

  string name;
  if(game->pak->attribute("megacd").boolean()) {
    //use Mega CD firmware settings
    std::vector<Firmware> firmware;
    for(auto& emulator : emulators) {
      if(emulator->name == "Mega CD") firmware = emulator->firmware;
    }
    if(firmware.empty()) return otherError;  //should never occur
    name = "Mega CD 32X";
    system = mia::System::create("Mega CD 32X");
    result = system->load(firmware[regionID].location);
    if(result != successful) {
      result.firmwareSystemName = "Mega CD 32X";
      result.firmwareType = firmware[regionID].type;
      result.firmwareRegion = firmware[regionID].region;
      result.result = noFirmware;
      return result;
    }

    disc = mia::Medium::create("Mega CD");
    if(disc->load(Emulator::load(disc, configuration.game)) != successful) disc.reset();

    //a "SEGA SSF" ROM (the Mega EverDrive PRO's SSF mode) with the Mega EverDrive PRO chosen in
    //Settings > Options: the ROM goes into that EverDrive and boots from it, as the PRO runs a ROM
    //from its SD card (the 32X+CD Plutonia cartridge writes its own memory). Any other ROM, or
    //another cart type in Options: a normal cartridge, as before.
    ramCart.reset();
    ramCartFile = {};
    if(settings.megadrive.ramCart && settings.megadrive.ramCartType == 1) {
      if(auto fp = game->pak->read("program.rom"); fp && fp->size() >= 0x200
      && !memory::compare(fp->data() + 0x100, "SEGA SSF", 8)) {
        std::vector<u8> rom(fp->data(), fp->data() + fp->size());
        ramCart = makeRamCartPak(settings.megadrive.ramCartSlot, rom, ramCartFile);
      }
    }
  } else {
    name = "Mega 32X";
    system = mia::System::create("Mega 32X");
    result = system->load();
    if(result != successful) return result;
  }

  ares::MegaDrive::option("Recompiler", !settings.general.forceInterpreter);
  ares::MegaDrive::option("32X Timing", settings.megadrive.realHardwareTiming ? "console" : "off");

  if(!ares::MegaDrive::load(root, {"[Sega] ", name, " (", region, ")"})) return otherError;

  if(auto port = root->find<ares::Node::Port>("Cartridge Slot")) {
    port->allocate();
    port->connect();
  }

  if(auto port = root->find<ares::Node::Port>("Mega CD/Disc Tray")) {
    port->allocate();
    port->connect();
  }

  if(auto port = root->find<ares::Node::Port>("Controller Port 1")) {
    port->allocate("Fighting Pad");
    port->connect();
  }

  if(auto port = root->find<ares::Node::Port>("Controller Port 2")) {
    port->allocate("Fighting Pad");
    port->connect();
  }

  return successful;
}

auto Mega32X::save() -> bool {
  root->save();
  system->save(system->location);
  if(ramCart) saveRamCartPak(ramCart, ramCartFile);  //the ROM ran from the EverDrive: its memory is the save
  else game->save(game->location);
  if(disc) disc->save(disc->location);
  return true;
}

auto Mega32X::unload() -> void {
  Emulator::unload();  //calls save(), which writes the EverDrive's memory
  ramCart.reset();
  ramCartFile = {};
}

auto Mega32X::pak(ares::Node::Object node) -> std::shared_ptr<vfs::directory> {
  if(node->name() == "Mega Drive") return system->pak;
  if(node->name() == "Mega Drive Cartridge") return ramCart ? ramCart : game->pak;
  if(node->name() == "Mega CD Disc" && disc) return disc->pak;
  return {};
}
