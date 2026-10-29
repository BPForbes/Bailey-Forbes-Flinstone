/* KQ frame helpers of net_kq_delivery.c (static), on net_codec.h's sticky writer and reader:
 * the writer stays failed once a put fails, the readers return 0 on a short read, and an
 * offer is a fixed 200-byte run of fields. Includes the .c to reach the statics. */
#include "net_kq_delivery.c"

#define ASSERT(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

size_t fl_net_server_member_count(const fl_net_server_t *srv) { (void)srv; return 0; }
const fl_net_server_member_t *fl_net_server_member_at(const fl_net_server_t *srv, size_t i)
{
    (void)srv; (void)i;
    return NULL;
}
void fl_net_server_set_kq_hook(fl_net_server_kq_hook_fn hook) { (void)hook; }
void fl_net_client_set_kq_hook(fl_net_client_kq_hook_fn hook) { (void)hook; }
fl_result_t fl_net_session_send_frame(fl_net_sock_handle_t h, uint8_t op, const uint8_t *p,
                                      uint16_t n)
{
    (void)h; (void)op; (void)p; (void)n;
    return FL_RESULT_OK;
}

static int test_sticky_frames(void)
{
    static uint8_t buf[FL_NET_SESSION_MAX_MSG];
    static uint8_t big[FL_NET_SESSION_MAX_MSG];
    fl_bytes_sticky_writer_t w;
    fl_bytes_reader_t r;
    uint16_t len;

    fl_bytes_sticky_init(&w, buf, (uint16_t)sizeof buf);
    (void)fl_bytes_sticky_put_u32(&w, 0x01020304u);
    ASSERT(!w.bad && w.w.len == 4u && buf[0] == 1u && buf[3] == 4u);

    /* A put that does not fit sets bad and writes nothing; later puts that would fit are skipped. */
    memset(big, 0xAB, sizeof big);
    (void)fl_bytes_sticky_put_bytes(&w, big, sizeof big);
    ASSERT(w.bad && w.w.len == 4u);
    (void)fl_bytes_sticky_put_u32(&w, 5u);
    ASSERT(w.bad && w.w.len == 4u);

    /* A string that does not fit: bad, and not even its length byte is written. */
    fl_bytes_sticky_init(&w, buf, (uint16_t)sizeof buf);
    (void)fl_bytes_sticky_put_bytes(&w, big, sizeof big - 2u);
    (void)fl_bytes_sticky_put_str8(&w, "abc");
    ASSERT(w.bad && w.w.len == sizeof big - 2u);

    fl_bytes_sticky_init(&w, buf, (uint16_t)sizeof buf);
    (void)fl_bytes_sticky_put_u32(&w, 7u);
    len = w.w.len;
    fl_bytes_reader_init(&r, buf, len);
    ASSERT(r_u32(&r) == 7u && !r.bad);
    ASSERT(r_u32(&r) == 0u && r.bad);
    ASSERT(r_u64(&r) == 0u && r.bad);
    return 0;
}

/* An offer is 16 + 32 + 32 + 16 + 64 bytes, a big-endian u64, then 32 bytes, in that order. */
static int test_offer_layout(void)
{
    static uint8_t buf[FL_NET_SESSION_MAX_MSG];
    kq_file_offer_t in, out;
    fl_bytes_sticky_writer_t w;
    fl_bytes_reader_t r;

    memset(&in, 0, sizeof in);
    memset(in.file_id, 0x11, sizeof in.file_id);
    memset(in.revision_id, 0x22, sizeof in.revision_id);
    memset(in.content_commitment, 0x33, sizeof in.content_commitment);
    memset(in.author_identity, 0x44, sizeof in.author_identity);
    memset(in.author_signature, 0x55, sizeof in.author_signature);
    in.topology_generation = 0x0102030405060708ull;
    memset(in.policy_hash, 0x66, sizeof in.policy_hash);

    fl_bytes_sticky_init(&w, buf, (uint16_t)sizeof buf);
    kq_put_offer(&w, &in);
    ASSERT(!w.bad && w.w.len == 200u);
    ASSERT(buf[0] == 0x11u && buf[15] == 0x11u && buf[16] == 0x22u && buf[48] == 0x33u);
    ASSERT(buf[80] == 0x44u && buf[96] == 0x55u && buf[159] == 0x55u);
    ASSERT(buf[160] == 0x01u && buf[167] == 0x08u && buf[168] == 0x66u && buf[199] == 0x66u);

    memset(&out, 0, sizeof out);
    fl_bytes_reader_init(&r, buf, w.w.len);
    kq_get_offer(&r, &out);
    ASSERT(!r.bad && fl_bytes_reader_remaining(&r) == 0u && memcmp(&in, &out, sizeof in) == 0);

    /* One byte short: the reader fails and stays failed. */
    fl_bytes_reader_init(&r, buf, 199u);
    kq_get_offer(&r, &out);
    ASSERT(r.bad);
    return 0;
}

int main(void)
{
    if (test_sticky_frames() || test_offer_layout())
        return 1;
    printf("test_kq_wire: ok\n");
    return 0;
}
