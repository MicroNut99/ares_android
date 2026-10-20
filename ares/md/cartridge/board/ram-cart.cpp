//MicroNut99 MD/32X RAM cart (4 MB), and the paged 16MB-PCB-CD32X board (8 / 16 MB).
//
//Battery-backed 16-bit SRAM that sits in the cartridge slot (on top of the 32X).
//It does not assert CART_IN, so with a Mega CD attached the console boots the disc,
//and the cart is seen:
//  - by the 68000 at 0x400000-0x7fffff (Mega CD mode 2); with the 32X enabled only
//    while RV = 1 (the Mega32X board above gates that), and through the 32X windows
//    0x880000 / 0x900000 (bank register 0xa15104) while RV = 0;
//  - by both SH2s at 0x02000000 (cached) / 0x22000000 (cache-through), read only.
//The CD32X boot stacks (Kobo, OpenLara, OpenJazz, Hurrican, SuperTux, ...) copy the
//game into it from the disc and run it from there.
//
//The 8 / 16 MB board splits the 4 MB cart window into two 2 MB windows:
//  window 0 = cart 0x000000-0x1fffff, window 1 = cart 0x200000-0x3fffff.
//A page (2 MB) is chosen per window by an even-byte (/UWR) write with the page on
//D8-D10 to 0xa130f0 (window 0) or 0xa130f2 (window 1). Power-on: window 0 shows
//page 0, window 1 shows page 1, which is exactly the 4 MB cart's layout.
//An 8 MB board decodes D8-D9 only, so pages 4-7 mirror pages 0-3.
//The 4 MB cart has no page register and ignores those writes.

struct RamCart : Interface {
  using Interface::Interface;
  Memory::Writable<n16> ram;
  n3 page[2];
  n3 pageMask;
  boolean pageable;
  boolean writeProtect;

  auto load() -> void override {
    u32 size = 4_MiB;
    if(pak) {
      if(auto sizeAttribute = pak->attribute("size").natural()) size = sizeAttribute;
      writeProtect = pak->attribute("writeProtect").boolean();
    }
    if(size != 4_MiB && size != 8_MiB && size != 16_MiB) size = 4_MiB;
    ram.allocate(size >> 1, 0xffff);
    pageable = size > 4_MiB;
    pageMask = size == 16_MiB ? 7 : 3;

    if(pak) {
      if(auto fp = pak->read("ram.cart")) {
        u32 words = min((u32)(fp->size() >> 1), ram.size());
        for(u32 address : range(words)) ram.write(address, fp->readm(2L));
      }
    }
  }

  auto save() -> void override {
    if(!pak) return;
    if(auto fp = pak->write("ram.cart")) {
      fp->seek(0);
      u32 words = min((u32)(fp->size() >> 1), ram.size());
      for(u32 address : range(words)) fp->writem(ram[address], 2L);
    }
  }

  auto offset(n24 address) const -> u32 {
    n22 local = address;  //the cart decodes A1-A21: 0x000000, 0x400000 and the SH2's 0x02000000 all land here
    u32 window = local.bit(21);
    u32 physical = (u32)page[window] << 21 | (u32)(local & 0x1fffff);
    return physical & (ram.size() * 2 - 1);
  }

  auto read(n1 upper, n1 lower, n24 address, n16 data) -> n16 override {
    return ram[offset(address) >> 1];
  }

  auto write(n1 upper, n1 lower, n24 address, n16 data) -> void override {
    if(writeProtect) return;  //the cart's switch in "hard lock": /UWR and /LWR cut
    u32 word = offset(address) >> 1;
    if(upper) ram[word].byte(1) = data.byte(1);
    if(lower) ram[word].byte(0) = data.byte(0);
  }

  auto readIO(n1 upper, n1 lower, n24 address, n16 data) -> n16 override {
    return data;
  }

  auto writeIO(n1 upper, n1 lower, n24 address, n16 data) -> void override {
    if(!pageable || !upper) return;  //the page latch sits on /UWR, D8-D10
    if(address < 0xa13000 || address > 0xa130ff) return;  //only /TIME space
    //the GreenPAK decodes CA1 and CA2 only: window 0 answers wherever (address & 6) == 0
    //(0xa130f0, 0xa13000, 0xa13008, ...), window 1 where it is 2 (0xa130f2, ...)
    if(address.bit(2)) return;
    page[address.bit(1)] = data.bit(8,10) & pageMask;
  }

  auto power(bool reset) -> void override {
    //the page latches are cleared by /VRES (power-on and reset)
    page[0] = 0;
    page[1] = 1;
  }

  auto serialize(serializer& s) -> void override {
    s(ram);
    s(page);
  }
};
