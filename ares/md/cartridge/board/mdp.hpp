//MD+ on the Mega EverDrive PRO: krikzz's mapper/lib_base/mdp.sv (mega-ed-pub, MIT), in every core built with
//MDP_ON (map_ssf, map_smd_cd and the cores built from them: OutRun / Mega Man X CD32X ...).  What is modelled, and
//where it comes from:
//  - from mdp.sv (mdp_md_regs, read): a word write of 0xCD54 at cart 0x03F7FA opens the registers (any other word
//    closes them); open, 0x03F7F6 / 0x03F7F8 read "RA" "TE", 0x03F7FC the answer, 0x03F7FE 0xFFFF while a command
//    runs (0 otherwise) and takes a command, one more waits behind it (cmd_buff); 0x03F800-0x03FFFF the data buffer
//    (modelled as zeros).  The registers answer before the mapper (everdrive.sv cpu_dati), for the 68000 and the SH2s
//    alike: open, the cart's own words at 0x03F7F6-0x03FFFF are not seen.
//  - from mdp.sv: the address is the cart bus's A1-A21, so a 68000 write at 0x03F7FA with the 32X on (RV = 1) reaches
//    it: OutRun CD32X V33 on the console (23 Nov 2026), although the EverDrive's memory there takes no such write.
//  - the commands, from the games that use it on the console (OutRun CD32X V30-V36): 0x11tt track tt once, 0x12tt
//    track tt repeated, 0x13xx stop.  Any other command: done, nothing played, one line printed - not known here.
//  - the tracks: <the game's name>.cue beside it on the SD card - the EverDrive's OS turns MD+ on then (OutRun CD32X
//    V33 on the console).  Track numbers as in the .cue.  BINARY (raw CD audio, 16-bit little-endian stereo 44.1 kHz)
//    and WAVE files; one file for the disc or one a track.
//  - not known, so a guess: how long a command takes (here: until the next vblank), the volume (full scale), what the
//    MCU sends in the answer word (0).
//ARES_MDP=0: MD+ off even with a .cue.  ARES_MDP_LOG=1: every register access and command printed.
struct MDPlus {
  struct Track { string file; bool wave = false; bool audio = false; u64 start = 0; u64 size = 0; };

  bool on = false;          //the OS's fea_mdp: a .cue beside the game
  bool open = false;        //ovl_on
  bool exec = false;        //cmd_exec
  bool waiting = false;     //cmd_ib
  u16 cmd = 0, next = 0, answer = 0;
  bool log = false;

  std::vector<Track> tracks;          //[track number]
  FILE* fp = nullptr;
  string fpName;
  u32 playing = 0;                    //0: silent
  bool repeat = false;
  u64 pos = 0;                        //bytes into the track
  f64 owed = 0;                       //samples a frame (44,100 / the frame rate), the fraction carried
  Node::Audio::Stream stream;

  static auto bytesWord(const u8* p) -> u32 { return p[0] | p[1] << 8 | p[2] << 16 | p[3] << 24; }

  //the cue: FILE "x" BINARY|WAVE / TRACK nn AUDIO|MODEx/2352 / INDEX 01 mm:ss:ff
  auto loadCue(const string& cuePath) -> bool {
    auto text = file::read(cuePath);
    if(text.empty()) return false;
    string dir = Location::path(cuePath);
    string curFile; bool curWave = false; u64 curFileSize = 0, waveData = 0;
    struct Pending { u32 n; string file; bool wave; bool audio; u64 start; u64 fileSize; };
    std::vector<Pending> list;
    u32 curTrack = 0; bool curAudio = false;
    for(auto line : nall::split(string{(const char*)text.data(), text.size()}.replace("\r", ""), "\n")) {
      line.strip();
      if(line.beginsWith("FILE ")) {
        auto q1 = line.find("\""), q2 = line.findFrom(q1 ? *q1 + 1 : 0, "\"");
        if(!q1 || !q2) continue;
        curFile = {dir, slice(line, *q1 + 1, *q2)};
        curWave = line.iendsWith("WAVE");
        curFileSize = file::size(curFile);
        waveData = 0;
        if(curWave) {   //the "data" chunk's start
          u8 h[12]; FILE* f = fopen(curFile, "rb");
          if(f && fread(h, 1, 12, f) == 12) {
            u64 at = 12; u8 c[8];
            while(fseek(f, at, SEEK_SET) == 0 && fread(c, 1, 8, f) == 8) {
              u32 len = bytesWord(c + 4);
              if(!memcmp(c, "data", 4)) { waveData = at + 8; curFileSize = min(curFileSize, waveData + len); break; }
              at += 8 + len + (len & 1);
            }
          }
          if(f) fclose(f);
        }
      } else if(line.beginsWith("TRACK ")) {
        auto parts = nall::split(line, " ");
        if(parts.size() < 3) continue;
        curTrack = parts[1].natural();
        curAudio = parts[2] == "AUDIO";
      } else if(line.beginsWith("INDEX 01 ") && curTrack) {
        string rest = slice(line, 9);
        auto msf = nall::split(rest, ":");
        if(msf.size() != 3) continue;
        u64 frame = (msf[0].natural() * 60 + msf[1].natural()) * 75 + msf[2].natural();
        list.push_back({curTrack, curFile, curWave, curAudio, waveData + frame * 2352, curFileSize});
      }
    }
    if(list.empty()) return false;
    tracks.clear();
    for(u32 i : range(list.size())) {
      auto& p = list[i];
      u64 end = p.fileSize;
      if(i + 1 < list.size() && list[i + 1].file == p.file) end = list[i + 1].start;
      if(p.n >= tracks.size()) tracks.resize(p.n + 1);
      tracks[p.n] = {p.file, p.wave, p.audio, p.start, end > p.start ? end - p.start : 0};
    }
    return true;
  }

  auto load(Node::Object parent, const string& cue) -> void {
    unload(parent);
    if(auto e = getenv("ARES_MDP"); e && string{e} == "0") { print("[MD+] off (ARES_MDP=0)\n"); return; }
    log = getenv("ARES_MDP_LOG") != nullptr;
    if(!cue || !loadCue(cue)) return;
    on = true;
    u32 audio = 0; for(auto& t : tracks) audio += t.audio;
    print("[MD+] on: ", cue, " - ", audio, " audio tracks (the EverDrive plays them when the game asks)\n");
    stream = parent->append<Node::Audio::Stream>("MD+");
    stream->setChannels(2);
    stream->setFrequency(44100);
  }

  auto unload(Node::Object parent) -> void {
    if(stream) { parent->remove(stream); stream.reset(); }
    if(fp) { fclose(fp); fp = nullptr; fpName = ""; }
    on = false; tracks.clear();
  }

  auto power() -> void {
    open = exec = waiting = false; cmd = next = answer = 0;
    playing = 0; pos = 0; owed = 0;
  }

  //a word the cart's bus sees (n22: A1-A21 and A0 = 0)
  auto write(u32 address, n16 data) -> void {
    if(!on) return;
    if(address == 0x3f7fa) {
      open = data == 0xcd54;
      if(log) print("[MD+] 03F7FA = ", hex(data, 4L), open ? " (open)\n" : " (closed)\n");
      return;
    }
    if(address == 0x3f7fe && open) {
      if(log) print("[MD+] command ", hex(data, 4L), exec ? (waiting ? " (one waits already: lost)\n" : " (waits)\n") : "\n");
      if(!exec) { cmd = data; exec = true; }
      else if(!waiting) { next = data; waiting = true; }
    }
  }

  auto read(u32 address, n16& value) -> bool {
    if(!on || !open) return false;
    if(address == 0x3f7fe) { value = exec ? 0xffff : 0x0000; return true; }
    if(address == 0x3f7fc) { value = answer; return true; }
    if(address == 0x3f7f6) { value = 0x5241; return true; }
    if(address == 0x3f7f8) { value = 0x5445; return true; }
    if(address >= 0x3f800 && address <= 0x3fffe) { value = 0; return true; }
    return false;
  }

  auto run(u16 c) -> void {
    u8 op = c >> 8, arg = c;
    if(op == 0x11 || op == 0x12) {
      if(arg >= tracks.size() || !tracks[arg].audio || !tracks[arg].size) {
        print("[MD+] play track ", arg, ": not an audio track of the .cue - nothing played\n");
        playing = 0; return;
      }
      playing = arg; repeat = op == 0x12; pos = 0;
      if(log) print("[MD+] track ", arg, repeat ? " repeated\n" : " once\n");
    } else if(op == 0x13) {
      playing = 0;
      if(log) print("[MD+] stop\n");
    } else {
      print("[MD+] command ", hex(c, 4L), ": not known to this model - done, nothing changed\n");
    }
  }

  //each frame: the command taken (the EverDrive's MCU: a guess, one a frame) and the frame's samples
  auto frame(f64 rate) -> void {
    if(!on) return;
    if(exec) {
      run(cmd);
      if(waiting) { cmd = next; waiting = false; } else exec = false;
    }
    owed += 44100.0 / rate;
    u32 n = (u32)owed; owed -= n;
    if(!stream) return;
    u8 buf[4 * 1024];
    while(n) {
      u32 k = min(n, 1024u), got = 0;
      if(playing) {
        auto& t = tracks[playing];
        if(fpName != t.file) {
          if(fp) fclose(fp);
          fp = fopen(t.file, "rb"); fpName = t.file;
        }
        u64 left = t.size - pos;
        u32 want = (u32)min<u64>((u64)k * 4, left & ~3ull);
        if(fp && fseek(fp, t.start + pos, SEEK_SET) == 0) got = fread(buf, 1, want, fp) & ~3u;
        pos += got;
        if(pos + 4 > t.size) { if(repeat) pos = 0; else playing = 0; }
        if(!got && !repeat) playing = 0;
      }
      for(u32 i : range(k)) {
        if(i * 4 + 4 <= got) {
          i16 l = (i16)(buf[i * 4] | buf[i * 4 + 1] << 8), r = (i16)(buf[i * 4 + 2] | buf[i * 4 + 3] << 8);
          stream->frame(l / 32768.0, r / 32768.0);
        } else {
          stream->frame(0.0, 0.0);
        }
      }
      n -= k;
    }
  }
};
