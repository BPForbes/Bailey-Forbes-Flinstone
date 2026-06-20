/*
 * WiFi driver ↔ P3 packet module bridge.
 */

#include "wifi_driver_packet.h"

#include "net_wire.h"

#include "fl/mem_asm.h"

fl_result_t wifi_driver_packet_validate_tx(const uint8_t *frame, size_t len)
{
	fl_net_frame_view_t view;
	fl_net_packet_t pkt;

	if (!frame || len < FL_NET_ETH_FRAME_HDR_LEN)
		return FL_RESULT_INVAL;

	view = fl_net_frame_view_make(frame, len);
	if (fl_net_wire_check_view(&view, FL_NET_ETH_FRAME_HDR_LEN) != FL_RESULT_OK)
		return FL_RESULT_INVAL;

	/* IPv4 data frames must parse cleanly; ARP / non-IPv4 pass L2 check only. */
	if (fl_net_packet_parse_eth_ipv4(frame, len, &pkt) == FL_RESULT_OK)
		return FL_RESULT_OK;

	if (len >= FL_NET_ETH_FRAME_HDR_LEN + 28u) {
		int ok = 0;
		uint16_t ethertype = fl_net_wire_ethertype_be16(frame, len, &ok);
		if (ok && ethertype == FL_ETHERTYPE_ARP)
			return FL_RESULT_OK;
	}

	return FL_RESULT_ERR;
}

fl_result_t wifi_driver_packet_ingest_rx(const uint8_t *frame, size_t len,
					 fl_net_pipeline_rx_t *pipe_out)
{
	fl_result_t rc;

	if (!pipe_out)
		return FL_RESULT_INVAL;
	if (!frame || len == 0)
		return FL_RESULT_INVAL;

	fl_net_pipeline_rx_reset(pipe_out);

	rc = fl_net_pipeline_rx_feed(pipe_out, FL_NET_PIPE_STAGE_DRV_RX, frame, len);
	if (rc != FL_RESULT_OK)
		return rc;

	rc = fl_net_pipeline_rx_feed(pipe_out, FL_NET_PIPE_STAGE_PARSE_L2, frame, len);
	if (rc != FL_RESULT_OK)
		return rc;

	rc = fl_net_pipeline_rx_feed(pipe_out, FL_NET_PIPE_STAGE_PARSE_L3, frame, len);
	if (rc == FL_RESULT_OK)
		(void)fl_net_pipeline_rx_feed(pipe_out, FL_NET_PIPE_STAGE_PARSE_L4, frame,
					      len);

	return FL_RESULT_OK;
}
