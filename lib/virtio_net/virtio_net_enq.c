/* SPDX-License-Identifier: Marvell-Proprietary
 * Copyright (c) 2024 Marvell.
 */

#include "dao_virtio_netdev.h"
#include "virtio_dev_priv.h"

#include "spec/virtio_net.h"

#include "virtio_net_priv.h"

dao_virtio_net_enq_fn_t dao_virtio_net_enq_fns[VIRTIO_NET_ENQ_OFFLOAD_LAST << 1] = {
#define T(name, flags)[flags] = virtio_net_enq_##name,
	VIRTIO_NET_ENQ_FASTPATH_MODES
#undef T
};

void
virtio_net_flush_enq(struct virtio_net_queue *q)
{
	uint16_t sd_mbuf_off = q->sd_mbuf_off;
	uint16_t last_off = q->last_off;
	uint16_t q_sz = q->q_sz;
	uint16_t pend;

	if (likely(q->auto_free))
		return;

	if (sd_mbuf_off != last_off) {
		pend = desc_off_diff_no_wrap(last_off, sd_mbuf_off, q_sz);
		rte_pktmbuf_free_bulk(&q->mbuf_arr[DESC_OFF(sd_mbuf_off)], pend);
		sd_mbuf_off = desc_off_add(sd_mbuf_off, pend, q_sz);
		pend = last_off - sd_mbuf_off;
		if (pend) {
			rte_pktmbuf_free_bulk(&q->mbuf_arr[DESC_OFF(sd_mbuf_off)], pend);
			sd_mbuf_off = desc_off_add(sd_mbuf_off, pend, q_sz);
		}
		q->sd_mbuf_off = sd_mbuf_off;
		q->pend_sd_mbuf = 0;
	}
}

static __rte_always_inline void
process_mseg_pkts_enq(struct virtio_net_queue *q, struct dao_dma_vchan_state *mem2dev,
		      struct rte_mbuf *mbuf, uint16_t *qoff, uint16_t nb_enq, uint16_t extra_desc,
		      uint16_t flags)
{
	uint64_t *sd_desc_base = q->sd_desc_base;
	struct rte_mbuf **mbuf_arr = q->mbuf_arr;
	uint16_t vhdr_sz = q->virtio_hdr_sz;
	uint16_t off = *qoff, cnt, moff;
	uint32_t slen, dlen, buf_len;
	uint64_t d_flags, avail;
	struct rte_mbuf *m_next;
	uint16_t q_sz = q->q_sz;
	uintptr_t hdr;

	slen = mbuf->pkt_len + vhdr_sz;

	/* Decrease the length of the source by the total length of the
	 * dummy descriptors added, where each newly added dummy descriptor
	 * will be of length 1
	 */

	if (flags & VIRTIO_NET_ENQ_OFFLOAD_NOFF)
		slen -= extra_desc;

	mbuf_arr[off] = mbuf;
	for (cnt = 0; cnt < nb_enq; cnt++) {
		d_flags = *DESC_PTR_OFF(sd_desc_base, off, 8);
		buf_len = (d_flags & (RTE_BIT64(32) - 1));

		if (flags & VIRTIO_NET_ENQ_OFFLOAD_NOFF)
			slen = slen ? slen : 1;

		d_flags = d_flags & 0xFFFFFFFF00000000UL;
		dlen = slen > buf_len ? buf_len : slen;

		avail = !!(d_flags & VIRT_PACKED_RING_DESC_F_AVAIL);
		d_flags &= ~VIRT_PACKED_RING_DESC_F_AVAIL_USED;

		/* Set both AVAIL and USED bit same and fillup length in Tx desc */
		*DESC_PTR_OFF(sd_desc_base, off, 8) =
			avail << 55 | avail << 63 | d_flags | (dlen & (RTE_BIT64(32) - 1));
		dao_dma_enq_dst_x1(mem2dev, *DESC_PTR_OFF(sd_desc_base, off, 0), dlen);
		off = (off + 1) & (q_sz - 1);
		mbuf_arr[off] = NULL;
		slen -= dlen;
	}

	moff = *qoff;
	hdr = rte_pktmbuf_mtod_offset(mbuf, uintptr_t, -vhdr_sz);
	slen = mbuf->data_len + vhdr_sz;
	dao_dma_enq_src_x1(mem2dev, hdr, slen);
	m_next = mbuf->next;
	mbuf->next = NULL;
	mbuf->nb_segs = 1;
	mbuf = m_next;
	while (unlikely(mbuf)) {
		hdr = rte_pktmbuf_mtod_offset(mbuf, uintptr_t, 0);
		dao_dma_enq_src_x1(mem2dev, hdr, mbuf->data_len);
		m_next = mbuf->next;
		if (flags & VIRTIO_NET_ENQ_OFFLOAD_NOFF) {
			moff = (moff + 1) & (q_sz - 1);
			mbuf_arr[moff] = mbuf;
		}
		mbuf->next = NULL;
		mbuf->nb_segs = 1;
		mbuf = m_next;
	}

	*qoff = off;
	return;
}

static __rte_always_inline uint16_t
mbuf_pkt_type_to_virtio_hash_report(uint8_t *hash_report, uint32_t packet_type)
{
	uint32_t index = (packet_type & (RTE_PTYPE_L3_MASK | RTE_PTYPE_L4_MASK)) >> 4;

	return (uint16_t)hash_report[index];
}

static __rte_always_inline uint32_t
calculate_nb_enq(uint64_t *sd_desc_base, uint16_t off, uint32_t slen, uint16_t qsize,
		 uint16_t avail_sd)
{
	uint16_t nb_enq = 0;
	uint32_t dlen = 0;
	uint64_t d_flags;

	while (dlen < slen && avail_sd) {
		d_flags = *DESC_PTR_OFF(sd_desc_base, off, 8);
		dlen += d_flags & (RTE_BIT64(32) - 1);
		off = (off + 1) & (qsize - 1);
		nb_enq += 1;
		avail_sd--;
	}
	return dlen >= slen ? nb_enq : UINT16_MAX;
}

static __rte_always_inline int
push_enq_data(struct virtio_net_queue *q, struct dao_dma_vchan_state *mem2dev,
	      struct rte_mbuf **mbufs, uint16_t nb_mbufs, const uint16_t flags)
{
	uint64x2_t rss0, rss1, rss2, rss3, d01, d23, rss0213;
	uint32_t pkt_typ0, pkt_typ1, pkt_typ2, pkt_typ3;
	uint64x2_t flags01, flags23, len01, len23;
	struct rte_mbuf **mbuf_arr = q->mbuf_arr;
	uint64_t *mbuf0, *mbuf1, *mbuf2, *mbuf3;
	uint16_t virtio_hdr_sz = q->virtio_hdr_sz;
	uint64_t *sd_desc_base = q->sd_desc_base;
	uint64_t *data0, *data1, *data2, *data3;
	uint16_t off = DESC_OFF(q->last_off);
	uint64x2_t mbuf01, mbuf23, buf01, buf23;
	uint64x2_t desc0, desc1, desc2, desc3;
	uint64x2_t dataoff_iova0, dataoff_iova1;
	uint64x2_t dataoff_iova2, dataoff_iova3;
	uint16_t last_idx = 0, mbuf_nb_segs = 0;
	uint16_t count, nb_enq, extra_desc = 0;
	uint64x2_t len_olflags0, len_olflags1;
	uint64x2_t len_olflags2, len_olflags3;
	uint16_t sd_off, avail_sd, avail_mbuf;
	uint32x4_t ol_flags, xlen, ylen, h0213;
	uint64x2_t xflags01, xflags23;
	uint8_t *hrp = q->hash_report;
	uint64x2_t vdst[4], vsrc[4];
	struct virtio_net_hdr *hdr;
	uint64x2_t xtmp0, xtmp1;
	uint16_t used = 0, i = 0;
	uint16_t q_sz = q->q_sz;
	uint64_t d_flags, avail;
	uint32_t len, buf_len;

	/* Check for minimum space */
	if (!dao_dma_flush(mem2dev, 1))
		goto exit;

	if (flags & VIRTIO_NET_ENQ_OFFLOAD_MSEG) {
		sd_off = __atomic_load_n(&q->sd_desc_off, __ATOMIC_ACQUIRE);
		avail_sd = desc_off_diff(sd_off, q->last_off, q->q_sz);
		avail_mbuf = q_sz - q->pend_sd_mbuf;
	}

	count = nb_mbufs & ~(0x3u);
	for (i = 0; i < count; ) {
		const uint64x2_t net_hdr_off = {
			virtio_hdr_sz,
			virtio_hdr_sz
		};
		const uint64x2_t xflags = {
			~(VIRT_PACKED_RING_DESC_F_USED | (RTE_BIT64(32) - 1)),
			~(VIRT_PACKED_RING_DESC_F_USED | (RTE_BIT64(32) - 1)),
		};
		const uint64x2_t xflags2 = {
			VIRT_PACKED_RING_DESC_F_AVAIL,
			VIRT_PACKED_RING_DESC_F_AVAIL,
		};

		if (unlikely(off + 3 >= q_sz))
			break;

		/* Move mbufs to iova */
		mbuf0 = (uint64_t *)mbufs[i];
		mbuf1 = (uint64_t *)mbufs[i + 1];
		mbuf2 = (uint64_t *)mbufs[i + 2];
		mbuf3 = (uint64_t *)mbufs[i + 3];

		if (i + 7 <= count) {
			rte_prefetch0(mbufs[i + 4]);
			rte_prefetch0(mbufs[i + 5]);
			rte_prefetch0(mbufs[i + 6]);
			rte_prefetch0(mbufs[i + 7]);
		}

		if (flags & VIRTIO_NET_ENQ_OFFLOAD_MSEG) {
			nb_enq = ((struct rte_mbuf *)mbuf0)->nb_segs +
				 ((struct rte_mbuf *)mbuf1)->nb_segs +
				 ((struct rte_mbuf *)mbuf2)->nb_segs +
				 ((struct rte_mbuf *)mbuf3)->nb_segs;
			/* Check for multi segs */
			if (nb_enq > 4)
				break;
		}

		dataoff_iova0 =
			vsetq_lane_u64(((struct rte_mbuf *)mbuf0)->data_off, vld1q_u64(mbuf0), 1);
		len_olflags0 = vld1q_u64(mbuf0 + 3);
		dataoff_iova1 =
			vsetq_lane_u64(((struct rte_mbuf *)mbuf1)->data_off, vld1q_u64(mbuf1), 1);
		len_olflags1 = vld1q_u64(mbuf1 + 3);
		dataoff_iova2 =
			vsetq_lane_u64(((struct rte_mbuf *)mbuf2)->data_off, vld1q_u64(mbuf2), 1);
		len_olflags2 = vld1q_u64(mbuf2 + 3);
		dataoff_iova3 =
			vsetq_lane_u64(((struct rte_mbuf *)mbuf3)->data_off, vld1q_u64(mbuf3), 1);
		len_olflags3 = vld1q_u64(mbuf3 + 3);

		/* Extract lengths */
		len01 = vzip2q_u64(len_olflags0, len_olflags1);
		len23 = vzip2q_u64(len_olflags2, len_olflags3);
		len01 = vshrq_n_u64(len01, 32);
		len23 = vshrq_n_u64(len23, 32);
		len01 += net_hdr_off;
		len23 += net_hdr_off;

		/* Get descriptor data for getting dest ptr */
		desc0 = vld1q_u64(DESC_PTR_OFF(sd_desc_base, off, 0));
		desc1 = vld1q_u64(DESC_PTR_OFF(sd_desc_base, off + 1, 0));
		desc2 = vld1q_u64(DESC_PTR_OFF(sd_desc_base, off + 2, 0));
		desc3 = vld1q_u64(DESC_PTR_OFF(sd_desc_base, off + 3, 0));

		flags01 = vzip2q_u64(desc0, desc1);
		flags23 = vzip2q_u64(desc2, desc3);

		if (flags & VIRTIO_NET_ENQ_OFFLOAD_MSEG) {
			xlen = vcgtq_u32(len01, flags01);
			ylen = vcgtq_u32(len23, flags23);
			xlen = vuzp1q_u32(xlen, ylen);
			nb_enq = vgetq_lane_u32(xlen, 0) ? 2 : 1;
			nb_enq += vgetq_lane_u32(xlen, 1) ? 2 : 1;
			nb_enq += vgetq_lane_u32(xlen, 2) ? 2 : 1;
			nb_enq += vgetq_lane_u32(xlen, 3) ? 2 : 1;
			if (nb_enq > 4)
				break;
		}

		/* Calculate data ptr for source */
		mbuf01 = vpaddq_u64(dataoff_iova0, dataoff_iova1);
		mbuf23 = vpaddq_u64(dataoff_iova2, dataoff_iova3);
		mbuf01 = mbuf01 - net_hdr_off;
		mbuf23 = mbuf23 - net_hdr_off;

		/* Update net header */
		data0 = (uint64_t *)vgetq_lane_u64(mbuf01, 0);
		data1 = (uint64_t *)vgetq_lane_u64(mbuf01, 1);
		data2 = (uint64_t *)vgetq_lane_u64(mbuf23, 0);
		data3 = (uint64_t *)vgetq_lane_u64(mbuf23, 1);

		if (flags & VIRTIO_NET_ENQ_OFFLOAD_CHECKSUM) {
			xflags01 = vzip1q_u64(len_olflags0, len_olflags1);
			xflags23 = vzip1q_u64(len_olflags2, len_olflags3);

			/* Pick lower 32 bits of olflags from each packet */
			ol_flags = vuzp1q_u32(xflags01, xflags23);

			/* Shift to extract RX_IP_CKSUM_BAD/RX_L4_CKSUM_BAD */
			ol_flags = vshrq_n_u8(ol_flags, 3);

			const uint64x2_t flag_mask = {
				0x0000000F0000000F,
				0x0000000F0000000F,
			};
			ol_flags = vandq_u32(ol_flags, flag_mask);

			/* If no Bad bits are set, return VIRTIO_NET_HDR_F_DATA_VALID */
			const uint8x16_t olflag_tbl = {
				2, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0,
			};
			ol_flags = vqtbl1q_u8(olflag_tbl, ol_flags);
			ol_flags = vandq_u32(ol_flags, flag_mask);

			*data0 = vgetq_lane_u32(ol_flags, 0);
			*data1 = vgetq_lane_u32(ol_flags, 1);
			*data2 = vgetq_lane_u32(ol_flags, 2);
			*data3 = vgetq_lane_u32(ol_flags, 3);
		} else {
			*data0 = 0;
			*data1 = 0;
			*data2 = 0;
			*data3 = 0;
		}

		if (flags & VIRTIO_NET_ENQ_OFFLOAD_HASH_REPORT) {
			pkt_typ0 = *((uint32_t *)mbuf0 + 8);
			pkt_typ1 = *((uint32_t *)mbuf1 + 8);
			pkt_typ2 = *((uint32_t *)mbuf2 + 8);
			pkt_typ3 = *((uint32_t *)mbuf3 + 8);

			rss0 = vld1q_u32((uint32_t *)mbuf0 + 11);
			rss1 = vld1q_u32((uint32_t *)mbuf1 + 11);
			rss2 = vld1q_u32((uint32_t *)mbuf2 + 11);
			rss3 = vld1q_u32((uint32_t *)mbuf3 + 11);

			h0213 = vdupq_n_u32(0);
			h0213 = vsetq_lane_u32(mbuf_pkt_type_to_virtio_hash_report(hrp, pkt_typ0),
					       h0213, 0);
			h0213 = vsetq_lane_u32(mbuf_pkt_type_to_virtio_hash_report(hrp, pkt_typ1),
					       h0213, 2);
			h0213 = vsetq_lane_u32(mbuf_pkt_type_to_virtio_hash_report(hrp, pkt_typ2),
					       h0213, 1);
			h0213 = vsetq_lane_u32(mbuf_pkt_type_to_virtio_hash_report(hrp, pkt_typ3),
					       h0213, 3);

			d01 = vzip1q_u64(rss0, rss1);
			d23 = vzip1q_u64(rss2, rss3);
			/* d01 elements are stored in even places  for transposet instr*/
			/* d23 elements are stored in odd places  for transposet instr*/
			rss0213 = vtrn1q_u32(d01, d23);

			const uint32x4_t def_hash_val = vdupq_n_u32(0);

			h0213 = vandq_u32(vcgtq_u32(rss0213, def_hash_val), h0213);

			d01 = vtrn1q_u32(rss0213, h0213);
			d23 = vtrn2q_u32(rss0213, h0213);

			*(uint64_t *)((uint32_t *)data0 + 3) = vgetq_lane_u64(d01, 0);
			*(uint64_t *)((uint32_t *)data1 + 3) = vgetq_lane_u64(d01, 1);
			*(uint64_t *)((uint32_t *)data2 + 3) = vgetq_lane_u64(d23, 0);
			*(uint64_t *)((uint32_t *)data3 + 3) = vgetq_lane_u64(d23, 1);
		}

		*(uint32_t *)(data0 + 1) = 0x10000;
		*(uint32_t *)(data1 + 1) = 0x10000;
		*(uint32_t *)(data2 + 1) = 0x10000;
		*(uint32_t *)(data3 + 1) = 0x10000;

		/* Take minimum of pktbuf and desc buf len.
		 * This also clears the flags portion of desc.
		 */
		len01 = vminq_u32(len01, flags01);
		len23 = vminq_u32(len23, flags23);

		buf01 = vzip1q_u64(desc0, desc1);
		buf23 = vzip1q_u64(desc2, desc3);

		/* Prepare descriptor flags */
		flags01 = flags01 & xflags;
		flags23 = flags23 & xflags;

		/* Extract AVAIL bits and move to USED */
		xtmp0 = flags01 & xflags2;
		xtmp1 = flags23 & xflags2;
		xtmp0 = xtmp0 << 8;
		xtmp1 = xtmp1 << 8;

		/* Set USED and len fields in flags */
		flags01 |= xtmp0;
		flags23 |= xtmp1;
		flags01 |= len01;
		flags23 |= len23;

		desc0 = vzip1q_u64(buf01, flags01);
		desc1 = vzip2q_u64(buf01, flags01);
		desc2 = vzip1q_u64(buf23, flags23);
		desc3 = vzip2q_u64(buf23, flags23);

		/* Prepare destination sg list */
		vdst[0] = vzip1q_u64(buf01, len01);
		vdst[1] = vzip2q_u64(buf01, len01);
		vdst[2] = vzip1q_u64(buf23, len23);
		vdst[3] = vzip2q_u64(buf23, len23);

		/* Prepare source sg list */
		vsrc[0] = vzip1q_u64(mbuf01, len01);
		vsrc[1] = vzip2q_u64(mbuf01, len01);
		vsrc[2] = vzip1q_u64(mbuf23, len23);
		vsrc[3] = vzip2q_u64(mbuf23, len23);

		mbuf_arr[off] = (struct rte_mbuf *)mbuf0;
		mbuf_arr[off + 1] = (struct rte_mbuf *)mbuf1;
		mbuf_arr[off + 2] = (struct rte_mbuf *)mbuf2;
		mbuf_arr[off + 3] = (struct rte_mbuf *)mbuf3;

		/* Write back descriptor and its flags */
		vst1q_u64(DESC_PTR_OFF(sd_desc_base, off, 0), desc0);
		vst1q_u64(DESC_PTR_OFF(sd_desc_base, off + 1, 0), desc1);
		vst1q_u64(DESC_PTR_OFF(sd_desc_base, off + 2, 0), desc2);
		vst1q_u64(DESC_PTR_OFF(sd_desc_base, off + 3, 0), desc3);

		nb_enq = dao_dma_enq_x4(mem2dev, vsrc, vdst);
#ifdef RTE_LIBRTE_MEMPOOL_DEBUG
		/* When fast free is enabled, all the buffers would be freed by DPI to NPA
		 * Mark them as put since SW didnot not be freeing them.
		 */
		if (!(flags & VIRTIO_NET_ENQ_OFFLOAD_NOFF))
			RTE_MEMPOOL_CHECK_COOKIES(mbufs[i]->pool, (void **)&mbufs[i], nb_enq, 0);
#endif
		i += nb_enq;
		used = i;
		off = (off + nb_enq) & (q_sz - 1);
		last_idx = mem2dev->tail;
		if (nb_enq != 4)
			break;
	}

	while (i < nb_mbufs) {
		mbuf0 = (uint64_t *)mbufs[i];

		/* Add Virtio header */
		hdr = rte_pktmbuf_mtod_offset((struct rte_mbuf *)mbuf0, struct virtio_net_hdr*,
					      -(virtio_hdr_sz));
		hdr->flags = 0;
		hdr->gso_type = 0;
		hdr->gso_size = 0;
		hdr->csum_start = 0;
		hdr->csum_offset = 0;

		if (flags & VIRTIO_NET_ENQ_OFFLOAD_CHECKSUM) {
			if (!(((struct rte_mbuf *)mbuf0)->ol_flags & RTE_MBUF_F_RX_IP_CKSUM_BAD) &&
			    !(((struct rte_mbuf *)mbuf0)->ol_flags & RTE_MBUF_F_RX_L4_CKSUM_BAD))
				hdr->flags = VIRTIO_NET_HDR_F_DATA_VALID;
		} else {
			hdr->flags = 0;
		}

		if (flags & VIRTIO_NET_ENQ_OFFLOAD_HASH_REPORT) {
			uint16_t hash_rpt;

			hash_rpt =
				mbuf_pkt_type_to_virtio_hash_report(hrp, *((uint32_t *)mbuf0 + 8));

			hdr->hash_value = ((struct rte_mbuf *)mbuf0)->hash.rss;
			hdr->hash_report = hdr->hash_value ? hash_rpt : 0;
		}

		d_flags = *DESC_PTR_OFF(sd_desc_base, off, 8);
		buf_len = d_flags & (RTE_BIT64(32) - 1);
		len = ((struct rte_mbuf *)mbuf0)->pkt_len + virtio_hdr_sz;

		if (flags & VIRTIO_NET_ENQ_OFFLOAD_MSEG) {
			if (likely(buf_len >= len))
				nb_enq = 1;
			else
				nb_enq = calculate_nb_enq(sd_desc_base, off, len, q_sz, avail_sd);

			if (flags & VIRTIO_NET_ENQ_OFFLOAD_NOFF) {
				mbuf_nb_segs = ((struct rte_mbuf *)mbuf0)->nb_segs;

				extra_desc = nb_enq < mbuf_nb_segs ? mbuf_nb_segs - nb_enq : 0;
				nb_enq += extra_desc;
			}

			last_idx = mem2dev->tail;
			/* Check for available descriptors and mbuf space */
			if (!dao_dma_flush(mem2dev, nb_enq) || nb_enq > avail_sd ||
			    nb_enq > avail_mbuf || nb_enq == UINT16_MAX)
				goto exit;

			hdr->num_buffers = nb_enq;

			avail_mbuf -= nb_enq;
			avail_sd -= nb_enq;
			process_mseg_pkts_enq(q, mem2dev, (struct rte_mbuf *)mbuf0, &off, nb_enq,
					      extra_desc, flags);
		} else {
			hdr->num_buffers = 1;
			d_flags = d_flags & 0xFFFFFFFF00000000UL;

			/* Limit length to buf len */
			len = len > buf_len ? buf_len : len;

			avail = !!(d_flags & VIRT_PACKED_RING_DESC_F_AVAIL);
			d_flags &= ~VIRT_PACKED_RING_DESC_F_AVAIL_USED;

			/* Set both AVAIL and USED bit same and fillup length in Tx desc */
			*DESC_PTR_OFF(sd_desc_base, off, 8) =
				avail << 55 | avail << 63 | d_flags | (len & (RTE_BIT64(32) - 1));
			mbuf_arr[off] = (struct rte_mbuf *)mbuf0;

			/* Prepare DMA src/dst of mbuf transfer */
			dao_dma_enq_x1(mem2dev, (uintptr_t)hdr, len,
				       *DESC_PTR_OFF(sd_desc_base, off, 0), len);
			off = (off + 1) & (q_sz - 1);
		}
#ifdef RTE_LIBRTE_MEMPOOL_DEBUG
		/* When fast free is enabled, all the buffers would be freed by DPI to NPA
		 * Mark them as put since SW didnot not be freeing them.
		 */
		if (!(flags & VIRTIO_NET_ENQ_OFFLOAD_NOFF))
			RTE_MEMPOOL_CHECK_COOKIES(mbufs[i]->pool, (void **)&mbufs[i], 1, 0);
#endif
		i++;
		used += hdr->num_buffers;

		last_idx = mem2dev->tail;
		/* Flush on reaching max SG limit */
		if (!dao_dma_flush(mem2dev, 1))
			goto exit;
	}

exit:
	if (used) {
		/* Update last offset index as per used mbufs */
		off = desc_off_add(q->last_off, used, q_sz);
		q->last_off = off;
		q->pend_sd_mbuf += used;
		dao_dma_update_cmpl_meta(mem2dev, &q->sd_mbuf_off, q->last_off, &q->pend_sd_mbuf,
					 used, last_idx);
	}
	return i;
}

static __rte_always_inline int
virtio_net_enq(struct virtio_net_queue *q, struct rte_mbuf **mbufs, uint16_t nb_mbufs,
	       const uint16_t flags)
{
	struct dao_dma_vchan_info *vchan_info = RTE_PER_LCORE(dao_dma_vchan_info);
	uint16_t dma_vchan = q->dma_vchan;
	struct dao_dma_vchan_state *mem2dev;
	uint16_t nb_used, sd_desc_off;
	uint16_t count;

	mem2dev = &vchan_info->mem2dev[dma_vchan];

	/* Fetch mem2dev DMA completed status */
	dao_dma_check_meta_compl(mem2dev, 1 /* ATOMIC update */);

	/* Send only mbufs as per available descriptors */
	sd_desc_off = __atomic_load_n(&q->sd_desc_off, __ATOMIC_ACQUIRE);
	count = desc_off_diff(sd_desc_off, q->last_off, q->q_sz);
	count = RTE_MIN(count, nb_mbufs);
	count = RTE_MIN(count, q->q_sz - q->pend_sd_mbuf);

	/* Return if no Tx descriptors are available */
	if (unlikely(!count))
		return 0;

	/* Validate descriptors */
	VIRTIO_NET_DESC_CHECK(q, q->last_off, count, true, false);

	/* Process mbuf transfer using DMA */
	nb_used = push_enq_data(q, mem2dev, mbufs, count, flags);

	return nb_used;
}

#define T(name, flags)                                                                             \
	uint16_t virtio_net_enq_##name(void *q, struct rte_mbuf **mbufs, uint16_t nb_mbufs)        \
	{                                                                                          \
		return virtio_net_enq(q, mbufs, nb_mbufs, (flags));                                \
	}

VIRTIO_NET_ENQ_FASTPATH_MODES
#undef T
