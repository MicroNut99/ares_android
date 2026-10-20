struct MegaCD32X : Emulator {
  MegaCD32X();
  auto load() -> LoadResult override;
  auto load(Menu) -> void override;
  auto unload() -> void override;
  auto save() -> bool override;
  auto pak(ares::Node::Object) -> std::shared_ptr<vfs::directory> override;

  auto ramCartLocation(u32 slot) -> string;
  auto saveRamCart() -> void;
  auto makeRamCart(u32 slot) -> void;
  auto insertRamCart(u32 slot) -> void;

  u32 regionID = 0;
  sTimer discTrayTimer;
  std::shared_ptr<vfs::directory> ramCart;  //MicroNut99 RAM cart / 16MB-PCB-CD32X in the cartridge slot
  string ramCartFile;
};

//16 MB carts 1-9, one file each, like separate physical carts: a cart is shared by every
//CD32X disc that runs with it. Kept in ares' Systems folder (next to ares.exe on Windows,
//or Settings > Paths > Home).
auto MegaCD32X::ramCartLocation(u32 slot) -> string {
  #if defined(PLATFORM_WINDOWS)
  string directory = settings.paths.home ? settings.paths.home : string{Path::program(), "Systems/"};
  #else
  string directory = mia::homeLocation();
  #endif
  directory::create(directory);
  string location{directory, "RAM Cart 16 MB-", slot, ".ram"};
  //the first build kept one cart as "RAM Cart 16 MB.ram": that becomes cart 1
  string previous{directory, "RAM Cart 16 MB.ram"};
  if(slot == 1 && !file::exists(location) && file::exists(previous)) file::move(previous, location);
  return location;
}

//the 16 MB cart `slot` (1-9), from its file; a new cart is blank (0xff)
auto MegaCD32X::makeRamCart(u32 slot) -> void {
  if(slot < 1 || slot > 9) slot = 1;
  const u32 size = 16_MiB;
  ramCartFile = ramCartLocation(slot);
  ramCart = std::make_shared<vfs::directory>();
  ramCart->setAttribute("title", "RAM Cart");
  ramCart->setAttribute("region", "NTSC-U, NTSC-J, PAL");
  ramCart->setAttribute("bootable", false);  //no CART_IN: the disc boots
  ramCart->setAttribute("mega32x", true);
  ramCart->setAttribute("ramcart", true);
  ramCart->setAttribute("size", size);
  ramCart->setAttribute("writeProtect", settings.megadrive.ramCartWriteProtect);
  std::vector<u8> memory(size, 0xff);
  auto existing = file::read(ramCartFile);
  if(!existing.empty()) memcpy(memory.data(), existing.data(), min(existing.size(), memory.size()));
  ramCart->append("ram.cart", memory);
}

//as on the console: power off, swap the cart, power on. The cart taken out is written to its file.
auto MegaCD32X::insertRamCart(u32 slot) -> void {
  Program::Guard guard;
  save();
  settings.megadrive.ramCart = true;
  settings.megadrive.ramCartSlot = slot;
  auto port = root->find<ares::Node::Port>("Cartridge Slot");
  if(!port) return;
  port->disconnect();
  makeRamCart(slot);
  port->allocate();
  port->connect();
  root->power();
  settingsWindow.optionSettings.refreshRamCart();
}

auto MegaCD32X::saveRamCart() -> void {
  if(!ramCart || !ramCartFile) return;
  if(auto fp = ramCart->read("ram.cart")) {
    file::write(ramCartFile, {fp->data(), fp->size()});
  }
}

MegaCD32X::MegaCD32X() {
  manufacturer = "Sega";
  name = "Mega CD 32X";

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

auto MegaCD32X::load() -> LoadResult {
  game = mia::Medium::create("Mega CD");
  string location = Emulator::load(game, configuration.game);
  if(!location) return noFileSelected;
  LoadResult result = game->load(location);
  if(result != successful) return result;

  auto region = Emulator::region();
  //if statements below are ordered by lowest to highest priority
  if(region == "PAL"   ) regionID = 2;
  if(region == "NTSC-J") regionID = 1;
  if(region == "NTSC-U") regionID = 0;

  //use Mega CD firmware settings
  std::vector<Firmware> firmware;
  for(auto& emulator : emulators) {
    if(emulator->name == "Mega CD") firmware = emulator->firmware;
  }
  if(firmware.empty()) return otherError;  //should never occur

  system = mia::System::create("Mega CD 32X");
  result = system->load(firmware[regionID].location);
  if(result != successful) {
    result.firmwareSystemName = "Mega CD";
    result.firmwareType = firmware[regionID].type;
    result.firmwareRegion = firmware[regionID].region;
    result.result = noFirmware;
    return result;
  }

  ares::MegaDrive::option("Recompiler", !settings.general.forceInterpreter);

  ramCart.reset();
  ramCartFile = {};
  if(settings.megadrive.ramCart) makeRamCart(settings.megadrive.ramCartSlot);

  if(!ares::MegaDrive::load(root, {"[Sega] Mega CD 32X (", region, ")"})) return otherError;

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
    if(game->pak->attribute("serial").beginsWith("GM T-16201F")) {
      // CORPSE KILLER 32X (U) -- GM T-16201F-00
      // CORPSE KILLER 32X (E) -- GM T-16201F-50
      // Gamepad in controller port 2 breaks input polling, so leave it disconnected.
      // No supported lightgun devices are currently emulated (Sega Menacer, ALG GameGun).
    } else {
      port->allocate("Fighting Pad");
      port->connect();
    }
  }

  discTrayTimer = Timer{};

  return successful;
}


auto MegaCD32X::load(Menu menu) -> void {
  Menu cartMenu{&menu};
  cartMenu.setIcon(Icon::Device::Storage);
  cartMenu.setText("RAM Cart");
  Group cartGroup;
  for(u32 slot : range(1, 10)) {
    MenuRadioItem item{&cartMenu};
    item.setText({"16 MB cart ", slot, file::exists(ramCartLocation(slot)) ? "" : " (blank)"});
    cartGroup.append(item);
    if(ramCart && slot == settings.megadrive.ramCartSlot) item.setChecked();
    item.onActivate([this, slot] {
      if(ramCart && slot == settings.megadrive.ramCartSlot) return;
      insertRamCart(slot);
      presentation.refreshSystemMenu();
    });
  }

  MenuItem changeDisc{&menu};
  changeDisc.setIcon(Icon::Device::Optical);
  changeDisc.setText("Change Disc").onActivate([&] {
    Program::Guard guard;
    save();
    auto tray = root->find<ares::Node::Port>("Mega CD/Disc Tray");
    tray->disconnect();

    if(game->load(Emulator::load(game, configuration.game)) != successful) {
      return;
    }

    //give the emulator core a few seconds to notice an empty drive state before reconnecting
    discTrayTimer->onActivate([&] {
      Program::Guard guard;
      discTrayTimer->setEnabled(false);
      auto tray = root->find<ares::Node::Port>("Mega CD/Disc Tray");
      tray->allocate();
      tray->connect();
    }).setInterval(3000).setEnabled();
  });
}

auto MegaCD32X::unload() -> void {
  Emulator::unload();  //calls save(), which writes the RAM cart
  discTrayTimer.reset();
  ramCart.reset();
  ramCartFile = {};
}

auto MegaCD32X::save() -> bool {
  root->save();  //the RAM cart board copies its SRAM into the pak here
  system->save(game->location);
  game->save(game->location);
  saveRamCart();
  return true;
}

auto MegaCD32X::pak(ares::Node::Object node) -> std::shared_ptr<vfs::directory> {
  if(node->name() == "Mega Drive") return system->pak;
  if(node->name() == "Mega CD Disc") return game->pak;
  if(node->name() == "Mega Drive Cartridge") return ramCart;
  return {};
}
