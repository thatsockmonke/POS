#include "pos_capabilities.h"
#include "pos_recorder.h"
#include "pos_appdata.h"
#include "pos_instruments.h"
#include "pos_files.h"
#include "pos_location.h"
#include "pos_music.h"
#include "pos_radio.h"
#include "pos_share.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static int fail(const pos_error_t *e) {
  fprintf(stderr, "%s\n", e->message);
  return 1;
}
static int cancelled(void *ctx, const char *phase, uint64_t done,
                     uint64_t total) {
  (void)phase;
  (void)total;
  return done >= (uint64_t)*(unsigned *)ctx;
}
typedef struct { unsigned config_bytes,delays; uint32_t crc; bool broken; uint8_t power; } imu_mock_t;
static int imu_read(void *ctx,uint8_t reg,uint8_t *bytes,size_t n) {
  imu_mock_t *m=ctx; memset(bytes,0,n);
  if(reg==0) *bytes=0x24;
  if(reg==0x21) *bytes=m->broken?0:1;
  if(reg==3) *bytes=0xc0;
  if(reg==0x0c) { bytes[1]=0x20; bytes[3]=0xe0; bytes[9]=0x40; }
  if(reg==0x22) bytes[1]=2;
  return 0;
}
static int imu_write(void *ctx,uint8_t reg,const uint8_t *bytes,size_t n) {
  imu_mock_t *m=ctx;
  if(reg==0x7e) { m->config_bytes=0; m->crc=0; }
  if(reg==0x5b) { assert(n==2); assert(((unsigned)bytes[0]+((unsigned)bytes[1]<<4))*2==m->config_bytes); }
  if(reg==0x5e) { assert(n<=32); m->config_bytes+=(unsigned)n; m->crc=pos_crc32(m->crc,bytes,n); }
  if(reg==0x7d) m->power=*bytes;
  return 0;
}
static void imu_delay(void *ctx,unsigned ms) { ((imu_mock_t *)ctx)->delays+=ms; }
int main(int argc, char **argv) {
  if (argc < 3)
    return 2;
  const char *mode = argv[1], *root = argv[2];
  pos_error_t e = {0};
  if (!strcmp(mode,"instruments")) {
    imu_mock_t mock={0}; pos_imu_bus_t bus={&mock,imu_read,imu_write,imu_delay};
    assert(!pos_imu_initialize(&bus,&e)); assert(mock.config_bytes==8192 && mock.crc==0x229a1189u && mock.delays>=100 && mock.power==14);
    pos_imu_sample_t sample;
    assert(!pos_imu_sample(&bus,&sample,&e));
    assert(sample.acceleration_g[0]==1.0f && sample.acceleration_g[1]==-1.0f && sample.temperature_c==24.0f);
    assert(sample.angular_dps[1]>499.9f && sample.angular_dps[1]<500.1f);
    assert(!pos_imu_suspend(&bus) && !mock.power);
    mock.broken=true; assert(pos_imu_initialize(&bus,&e)<0 && !mock.power);
    uint16_t pairs[34][2]; pos_ir_nec(0x12,0x34,pairs);
    assert(pairs[0][0]==9000 && pairs[0][1]==4500 && pairs[33][0]==560);
    uint32_t value=0; for(unsigned i=0;i<32;++i) { assert(pairs[i+1][0]==560); if(pairs[i+1][1]==1690) value|=1UL<<i; else assert(pairs[i+1][1]==560); }
    assert(value==0xcb34ed12UL); puts("verified config, axes, failure and NEC bits"); return 0;
  }
  if (!strcmp(mode,"recorder")) {
    if (!strcmp(argv[3],"recover")) return pos_recorder_recover(root,argv[4],&e)?fail(&e):0;
    pos_recorder_t *r=pos_recorder_open(root,&e); if(!r) return fail(&e);
    int16_t samples[256]; for(unsigned i=0;i<256;++i) samples[i]=(int)i*251-32000;
    for(unsigned i=0;i<150;++i) assert(!pos_recorder_write(r,samples,256,&e));
    assert(pos_recorder_frames(r)==38400);
    if(!strcmp(argv[3],"corrupt")) {
      char rel[260],path[POS_FS_PATH]; snprintf(rel,sizeof(rel),"%s.part",pos_recorder_path(r)); assert(!pos_fs_join(path,root,rel));
      FILE *f=fopen(path,"r+b"); assert(f); assert(!fseek(f,44,SEEK_SET)); fputc(1,f); fclose(f);
    }
    int result=0;
    if(strcmp(argv[3],"partial")) result=pos_recorder_finish(r,&e);
    puts(pos_recorder_path(r)); pos_recorder_close(r); return result?fail(&e):0;
  }
  if (!strcmp(mode,"counter")) {
    int count=0; int result=pos_counter_load(root,&count,&e);
    if(result<0) return fail(&e);
    if(argc>3) { count=atoi(argv[3]); if(pos_counter_save(root,count,&e)) return fail(&e); }
    printf("%d\n",count); return 0;
  }
  if (!strcmp(mode, "files")) {
    if (argc < 5)
      return 2;
    const char *op = argv[3], *source = argv[4];
    int result = -1;
    if (!strcmp(op, "copy"))
      result = pos_files_copy(root, source, argv[5], NULL, NULL, &e);
    if (!strcmp(op, "cancel-copy")) {
      unsigned at = 4096;
      result = pos_files_copy(root, source, argv[5], cancelled, &at, &e);
    }
    if (!strcmp(op, "move"))
      result = pos_files_move(root, source, argv[5], &e);
    if (!strcmp(op, "folder"))
      result = pos_files_folder(root, source, &e);
    if (!strcmp(op, "trash")) {
      char entry[241];
      result = pos_files_trash(root, source, entry, &e);
      if (!result)
        puts(entry);
    }
    if (!strcmp(op, "restore"))
      result = pos_files_restore(root, source, &e);
    if (!strcmp(op, "remove"))
      result = pos_files_remove(root, source, NULL, NULL, &e);
    if (!strcmp(op, "mutable"))
      return pos_files_mutable(root, source) ? 0 : 1;
    return result ? fail(&e) : 0;
  }
  if (!strcmp(mode, "share")) {
    char path[POS_FS_PATH];
    pos_fs_join(path, root, "offer.bin");
    if (argc < 4)
      return 2;
    const char *op = argv[3];
    if (!strcmp(op, "offer")) {
      pos_share_offer_t offer;
      if (pos_share_offer(root, argv[4], &offer, &e))
        return fail(&e);
      uint8_t wire[POS_SHARE_OFFER_BYTES];
      assert(!pos_share_encode(&offer, wire));
      assert(!pos_fs_write_new(path, wire, sizeof(wire)));
      return 0;
    }
    uint8_t wire[POS_SHARE_OFFER_BYTES + 1];
    FILE *f = fopen(path, "rb");
    assert(f);
    size_t n = fread(wire, 1, sizeof(wire), f);
    fclose(f);
    pos_share_offer_t offer;
    if (pos_share_decode(wire, n, &offer))
      return 1;
    if (!strcmp(op, "fragments")) {
      pos_share_assembly_t assembly = {0};
      pos_share_offer_t decoded;
      for (unsigned i = 0, at = 0; at < sizeof(wire) - 1; ++i) {
        uint8_t fragment[20] = {'O', i};
        unsigned length = POS_SHARE_OFFER_BYTES - at;
        if (length > 18)
          length = 18;
        memcpy(fragment + 2, wire + at, length);
        int result =
            pos_share_assemble(&assembly, fragment, length + 2, &decoded);
        at += length;
        assert(result == (at == POS_SHARE_OFFER_BYTES ? 1 : 0));
      }
      assert(!strcmp(decoded.name, offer.name) && decoded.id == offer.id);
      uint8_t wrong[] = {'O', 3, 1};
      assert(pos_share_assemble(&assembly, wrong, sizeof(wrong), &decoded) < 0);
      return 0;
    }
    pos_share_receive_t *r = calloc(1, sizeof(*r));
    assert(r);
    int acceptance = pos_share_accept(r, root, &offer, &e);
    if (acceptance < 0) {
      free(r);
      return fail(&e);
    }
    printf("%u\n", r->offset);
    if (!strcmp(op, "offset")) {
      pos_share_cancel(r);
      free(r);
      return 0;
    }
    if (!strcmp(op, "finish")) {
      int result = pos_share_finish(r, NULL, NULL, &e);
      pos_share_cancel(r);
      free(r);
      return result ? fail(&e) : 0;
    }
    if (!strcmp(op, "write") || !strcmp(op, "replay") ||
        !strcmp(op, "wrong-offset")) {
      char source[POS_FS_PATH];
      pos_fs_join(source, root, "Documents/source.bin");
      f = fopen(source, "rb");
      assert(f);
      unsigned amount = argc > 4 ? strtoul(argv[4], NULL, 10) : offer.size;
      uint8_t data[128];
      int result = 0;
      if (!strcmp(op, "replay")) {
        assert(fread(data, 1, 32, f) == 32);
        result = pos_share_write(r, 0, data, 32, &e);
      } else if (!strcmp(op, "wrong-offset")) {
        result = pos_share_write(r, r->offset + 1, "x", 1, &e);
      } else {
        assert(!fseek(f, r->offset, SEEK_SET));
        unsigned sent = 0;
        while (r->offset < offer.size && sent < amount) {
          size_t length = offer.size - r->offset;
          if (length > sizeof(data))
            length = sizeof(data);
          if (length > amount - sent)
            length = amount - sent;
          assert(fread(data, 1, length, f) == length);
          if (pos_share_write(r, r->offset, data, length, &e)) {
            result = -1;
            break;
          }
          sent += length;
        }
      }
      fclose(f);
      pos_share_cancel(r);
      free(r);
      return result ? fail(&e) : 0;
    }
    pos_share_cancel(r);
    free(r);
    return 2;
  }
  if (!strcmp(mode, "music")) {
    if (argc < 4)
      return 2;
    if (!strcmp(argv[3], "track")) {
      char path[241] = "";
      unsigned count = 0;
      int result = pos_music_track(root, argv[4], strtoul(argv[5], NULL, 10),
                                   path, &count);
      printf("%u\n%s\n", count, path);
      return result < 0 ? 1 : 0;
    }
    if (!strcmp(argv[3], "add"))
      return pos_music_playlist_add(root, argv[4], argv[5], &e) ? fail(&e) : 0;
    if (!strcmp(argv[3], "playlist")) {
      char path[241] = "";
      unsigned count = 0;
      int result = pos_music_playlist_track(
          root, argv[4], strtoul(argv[5], NULL, 10), path, &count);
      printf("%u\n%s\n", count, path);
      return result < 0 ? 1 : 0;
    }
    pos_music_t *m = pos_music_open(root, argv[3], &e);
    if (!m)
      return fail(&e);
    char path[POS_FS_PATH];
    pos_fs_join(path, root, "decoded.raw");
    FILE *out = fopen(path, "wbx");
    assert(out);
    unsigned volume = argc > 4 ? strtoul(argv[4], NULL, 10) : 100;
    uint64_t frames = 0;
    for (unsigned i = 0; i < 1000000; ++i) {
      const int16_t *pcm;
      size_t n;
      int result = pos_music_read(m, &pcm, &n, volume, &e);
      if (result < 0) {
        fclose(out);
        pos_music_close(m);
        return fail(&e);
      }
      assert(fwrite(pcm, 4, n, out) == n);
      frames += n;
      pos_music_status_t status = pos_music_status(m);
      if (status.ended) {
        printf("%u %u %llu %u\n", status.rate, status.channels,
               (unsigned long long)frames, status.total_bytes);
        break;
      }
    }
    fclose(out);
    pos_music_close(m);
    return 0;
  }
  if (!strcmp(mode, "gps")) {
    char path[POS_FS_PATH];
    pos_fs_join(path, root, "gps.nmea");
    FILE *f = fopen(path, "rb");
    assert(f);
    pos_nmea_t p = {0};
    uint8_t bytes[128];
    size_t n;
    unsigned chunk = strtoul(argv[3], NULL, 10);
    assert(chunk && chunk <= 128);
    while ((n = fread(bytes, 1, chunk, f)))
      pos_nmea_feed(&p, bytes, n, 1000);
    fclose(f);
    pos_location_t location =
        pos_location_snapshot(&p, strtoull(argv[4], NULL, 10));
    printf("{\"fix\":%s,\"latitude\":%.9f,\"longitude\":%.9f,\"satellites\":%u,"
           "\"hdop\":%.3f,\"utc\":%lld,\"sentences\":%u}\n",
           location.fix ? "true" : "false", location.latitude,
           location.longitude, location.satellites, location.hdop,
           (long long)location.utc, p.sentences);
    return 0;
  }
  if (!strcmp(mode, "capabilities")) {
    for (unsigned bits = 0; bits < 8; ++bits) {
      bool cap = bits & 1, gps = bits & 2, enabled = bits & 4;
      assert(pos_feature_allowed(POS_FEATURE_RADIO, cap, gps, enabled) ==
             (cap && enabled));
      assert(pos_feature_allowed(POS_FEATURE_RANGE_TEST, cap, gps, enabled) ==
             (cap && enabled));
      assert(pos_feature_allowed(POS_FEATURE_MAPS, cap, gps, enabled) == gps);
    }
    assert(!pos_feature_allowed(99, true, true, true));
    return 0;
  }
  if (!strcmp(mode, "outbox")) {
    char path[POS_FS_PATH];
    pos_fs_join(path, root, "POS/Users/Owner/Radio/outbox");
    assert(!pos_fs_mkdirs(path));
    pos_radio_packet_t packet = {.type = POS_RADIO_MSG,
                                 .sender = 1,
                                 .destination = 2,
                                 .id = strtoul(argv[3], NULL, 16)};
    snprintf(packet.text, sizeof(packet.text), "%s", argv[4]);
    return pos_radio_store(root, &packet, &e) ? fail(&e) : 0;
  }
  if (!strcmp(mode, "radio")) {
    pos_radio_state_t a = {.node = 1}, b = {.node = 2};
    pos_radio_packet_t p = {.type = POS_RADIO_MSG,
                            .sender = 1,
                            .destination = 2,
                            .id = 123},
                       decoded, ack;
    strcpy(p.text, "hello Panputer");
    uint8_t wire[POS_RADIO_WIRE_MAX];
    size_t n = pos_radio_encode(&p, wire);
    assert(n == 37);
    assert(!pos_radio_decode(wire, n, &decoded));
    assert(!strcmp(decoded.text, p.text));
    for (size_t i = 0; i < n; ++i) {
      wire[i] ^= 1;
      assert(pos_radio_decode(wire, n, &decoded));
      wire[i] ^= 1;
    }
    assert(!pos_radio_enqueue(&a, &p, 0));
    assert(pos_radio_enqueue(&a, &p, 0));
    assert(pos_radio_due(&a, 0, &decoded) == 1);
    assert(a.sent == 1);
    assert(!pos_radio_due(&a, 14999, &decoded));
    assert(pos_radio_receive(&b, &p, &ack) == 1 && ack.type == POS_RADIO_ACK &&
           ack.destination == 1);
    assert(!pos_radio_receive(&b, &p, &ack) && b.duplicates == 1 &&
           b.received == 1);
    pos_radio_packet_t wrong = ack;
    wrong.sender = 3;
    assert(!pos_radio_receive(&a, &wrong, &decoded) && a.acked == 0);
    assert(!pos_radio_receive(&a, &ack, &decoded) && a.acked == 1);
    assert(!pos_radio_receive(&a, &ack, &decoded) && a.acked == 1);
    assert(!pos_radio_due(&a, 100000, &decoded));
    p.id = 124;
    assert(!pos_radio_enqueue(&a, &p, 0));
    assert(pos_radio_due(&a, 0, &decoded) == 1);
    assert(pos_radio_due(&a, 15000, &decoded) == 1);
    assert(pos_radio_due(&a, 45000, &decoded) == 1);
    assert(pos_radio_due(&a, 90000, &decoded) == 1);
    assert(pos_radio_due(&a, 150000, &decoded) == -1 && a.failed == 1);
    p.destination = 0;
    p.id = 125;
    assert(pos_radio_receive(&b, &p, &ack) == 1 && !ack.type);
    assert(!pos_radio_enqueue(&a, &p, 0));
    assert(pos_radio_due(&a, 0, &decoded) == 1 &&
           !pos_radio_due(&a, 999999, &decoded));
    char path[POS_FS_PATH];
    pos_fs_join(path, root, "range.csv");
    FILE *f = fopen(path, "wx");
    assert(f);
    assert(!pos_range_header(f));
    pos_location_t location = {.fix = false};
    assert(!pos_range_row(f, "TX", &p, &location, 0, 0, &a, 1000));
    location.fix = true;
    location.latitude = 34;
    location.longitude = -118;
    location.satellites = 8;
    location.hdop = 1.2;
    assert(!pos_range_row(f, "RX", &p, &location, -91, 3.5, &a, 2000));
    fclose(f);
    double distance = pos_location_distance(0, 0, 0, 1);
    assert(distance > 111000 && distance < 112000);
    return 0;
  }
  return 2;
}
