/*
 *   BSD LICENSE
 *
 *   Copyright (C) Semihalf 2017.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Semihalf nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdbool.h>

#include <rte_common.h>
#include <rte_hexdump.h>
#include <rte_cryptodev.h>
#include <rte_cryptodev_pmd.h>
#include <rte_vdev.h>
#include <rte_malloc.h>
#include <rte_cpuflags.h>

#include "rte_mrvl_pmd_private.h"

/**
 * Flag if particular crypto algorithm is supported by PMD/MUSDK.
 *
 * The idea is to have Not Supported value as default (0).
 * This way we need only to define proper map sizes,
 * non-initialized entries will be by default not supported.
 */
enum algo_supported {
	ALGO_NOT_SUPPORTED = 0,
	ALGO_SUPPORTED = 1,
};

/** Map elements for cipher mapping.*/
struct cipher_params_mapping {
	enum algo_supported  supported;   /**< On/Off switch */
	enum sam_cipher_alg  cipher_alg;  /**< Cipher algorithm */
	enum sam_cipher_mode cipher_mode; /**< Cipher mode */
	unsigned max_key_len;             /**< Maximum key length (in bytes)*/
}
/* We want to squeeze in multiple maps into the cache line. */
__rte_aligned(32);

/** Map elements for auth mapping.*/
struct auth_params_mapping {
	enum algo_supported supported;  /**< On/off switch */
	enum sam_auth_alg   auth_alg;   /**< Auth algorithm */
}
/* We want to squeeze in multiple maps into the cache line. */
__rte_aligned(32);

/**
 * Map of supported cipher algorithms.
 */
static const
struct cipher_params_mapping cipher_map[RTE_CRYPTO_CIPHER_LIST_END] = {
	[RTE_CRYPTO_CIPHER_3DES_CBC] = {
		.supported = ALGO_SUPPORTED,
		.cipher_alg = SAM_CIPHER_3DES,
		.cipher_mode = SAM_CIPHER_CBC,
		.max_key_len = BITS2BYTES(192) },
	[RTE_CRYPTO_CIPHER_3DES_CTR] = {
		.supported = ALGO_SUPPORTED,
		.cipher_alg = SAM_CIPHER_3DES,
		.cipher_mode = SAM_CIPHER_CTR,
		.max_key_len = BITS2BYTES(192) },
	[RTE_CRYPTO_CIPHER_3DES_ECB] = {
		.supported = ALGO_SUPPORTED,
		.cipher_alg = SAM_CIPHER_3DES,
		.cipher_mode = SAM_CIPHER_ECB,
		.max_key_len = BITS2BYTES(192) },
	[RTE_CRYPTO_CIPHER_AES_CBC] = {
		.supported = ALGO_SUPPORTED,
		.cipher_alg = SAM_CIPHER_AES,
		.cipher_mode = SAM_CIPHER_CBC,
		.max_key_len = BITS2BYTES(256) },
	[RTE_CRYPTO_CIPHER_AES_GCM] = {
		.supported = ALGO_SUPPORTED,
		.cipher_alg = SAM_CIPHER_AES,
		.cipher_mode = SAM_CIPHER_GCM,
		.max_key_len = BITS2BYTES(256) },
	[RTE_CRYPTO_CIPHER_AES_CTR] = {
		.supported = ALGO_SUPPORTED,
		.cipher_alg = SAM_CIPHER_AES,
		.cipher_mode = SAM_CIPHER_CTR,
		.max_key_len = BITS2BYTES(256) },
};

/**
 * Map of supported auth algorithms.
 */
static const
struct auth_params_mapping auth_map[RTE_CRYPTO_AUTH_LIST_END] = {
	[RTE_CRYPTO_AUTH_MD5_HMAC] = {
		.supported = ALGO_SUPPORTED,
		.auth_alg = SAM_AUTH_HMAC_MD5 },
	[RTE_CRYPTO_AUTH_MD5] = {
		.supported = ALGO_SUPPORTED,
		.auth_alg = SAM_AUTH_HASH_MD5 },
	[RTE_CRYPTO_AUTH_SHA1_HMAC] = {
		.supported = ALGO_SUPPORTED,
		.auth_alg = SAM_AUTH_HMAC_SHA1 },
	[RTE_CRYPTO_AUTH_SHA1] = {
		.supported = ALGO_SUPPORTED,
		.auth_alg = SAM_AUTH_HASH_SHA1 },
		/* No support for HMAC 224 yet in MUSDK crypto. */
	[RTE_CRYPTO_AUTH_SHA224_HMAC] = {
		.supported = ALGO_NOT_SUPPORTED,
		.auth_alg = SAM_AUTH_HMAC_SHA2_224 },
	[RTE_CRYPTO_AUTH_SHA224] = {
		.supported = ALGO_SUPPORTED,
		.auth_alg = SAM_AUTH_HASH_SHA2_224 },
	[RTE_CRYPTO_AUTH_SHA256_HMAC] = {
		.supported = ALGO_SUPPORTED,
		.auth_alg = SAM_AUTH_HMAC_SHA2_256 },
	[RTE_CRYPTO_AUTH_SHA256] = {
		.supported = ALGO_SUPPORTED,
		.auth_alg = SAM_AUTH_HASH_SHA2_256 },
	[RTE_CRYPTO_AUTH_SHA384_HMAC] = {
		.supported = ALGO_SUPPORTED,
		.auth_alg = SAM_AUTH_HMAC_SHA2_384 },
	[RTE_CRYPTO_AUTH_SHA384] = {
		.supported = ALGO_SUPPORTED,
		.auth_alg = SAM_AUTH_HASH_SHA2_384 },
	[RTE_CRYPTO_AUTH_SHA512_HMAC] = {
		.supported = ALGO_SUPPORTED,
		.auth_alg = SAM_AUTH_HMAC_SHA2_512 },
	[RTE_CRYPTO_AUTH_SHA512] = {
		.supported = ALGO_SUPPORTED,
		.auth_alg = SAM_AUTH_HASH_SHA2_512 },
	[RTE_CRYPTO_AUTH_AES_GCM] = {
		.supported = ALGO_SUPPORTED,
		.auth_alg = SAM_AUTH_AES_GCM },
	[RTE_CRYPTO_AUTH_AES_GMAC] = {
		.supported = ALGO_SUPPORTED,
		.auth_alg = SAM_AUTH_AES_GMAC },
};

/*
 *-----------------------------------------------------------------------------
 * Forward declarations.
 *-----------------------------------------------------------------------------
 */
static int cryptodev_mrvl_crypto_uninit(struct rte_vdev_device *vdev);

/*
 *-----------------------------------------------------------------------------
 * Session Preparation.
 *-----------------------------------------------------------------------------
 */

/**
 * Get xform chain order.
 *
 * @param xform Pointer to configuration structure chain for crypto operations.
 * @returns Order of crypto operations.
 */
static enum mrvl_crypto_chain_order
mrvl_crypto_get_chain_order(const struct rte_crypto_sym_xform *xform)
{
	/* Currently, Marvell supports max 2 operations in chain */
	if (xform->next != NULL && xform->next->next != NULL)
		return MRVL_CRYPTO_CHAIN_NOT_SUPPORTED;

	if (xform->next != NULL) {
		if ((xform->type == RTE_CRYPTO_SYM_XFORM_AUTH) &&
			(xform->next->type == RTE_CRYPTO_SYM_XFORM_CIPHER))
			return MRVL_CRYPTO_CHAIN_AUTH_CIPHER;

		if ((xform->type == RTE_CRYPTO_SYM_XFORM_CIPHER) &&
			(xform->next->type == RTE_CRYPTO_SYM_XFORM_AUTH))
			return MRVL_CRYPTO_CHAIN_CIPHER_AUTH;
	} else {
		if (xform->type == RTE_CRYPTO_SYM_XFORM_AUTH)
			return MRVL_CRYPTO_CHAIN_AUTH_ONLY;

		if (xform->type == RTE_CRYPTO_SYM_XFORM_CIPHER)
			return MRVL_CRYPTO_CHAIN_CIPHER_ONLY;
	}

	return MRVL_CRYPTO_CHAIN_NOT_SUPPORTED;
}

/**
 * Set session parameters for cipher part.
 *
 * @param sess Crypto session pointer.
 * @param cipher_xform Pointer to configuration structure for cipher operations.
 * @returns 0 in case of success, negative value otherwise.
 */
static int
mrvl_crypto_set_cipher_session_parameters(struct mrvl_crypto_session *sess,
		const struct rte_crypto_sym_xform *cipher_xform)
{
	/* Make sure we've got proper struct */
	if (cipher_xform->type != RTE_CRYPTO_SYM_XFORM_CIPHER) {
		MRVL_CRYPTO_LOG_ERR("Wrong xform struct provided!");
		return -EINVAL;
	}

	/* See if map data is present and valid */
	if ((cipher_xform->cipher.algo > RTE_DIM(cipher_map)) ||
		(cipher_map[cipher_xform->cipher.algo].supported
			!= ALGO_SUPPORTED)) {
		MRVL_CRYPTO_LOG_ERR("Cipher algorithm not supported!");
		return -EINVAL;
	}

	sess->sam_sess_params.cipher_alg =
		cipher_map[cipher_xform->cipher.algo].cipher_alg;
	sess->sam_sess_params.cipher_mode =
		cipher_map[cipher_xform->cipher.algo].cipher_mode;

	/* Assume IV will be passed together with data. */
	sess->sam_sess_params.cipher_iv = NULL;

	/* Get max key length. */
	if (cipher_xform->cipher.key.length >
		cipher_map[cipher_xform->cipher.algo].max_key_len) {
		MRVL_CRYPTO_LOG_ERR("Wrong key length!");
		return -EINVAL;
	}

	sess->sam_sess_params.cipher_key_len = cipher_xform->cipher.key.length;
	sess->sam_sess_params.cipher_key = cipher_xform->cipher.key.data;
	return 0;
}

/**
 * Set session parameters for authentication part.
 *
 * @param sess Crypto session pointer.
 * @param auth_xform Pointer to configuration structure for auth operations.
 * @returns 0 in case of success, negative value otherwise.
 */
static int
mrvl_crypto_set_auth_session_parameters(struct mrvl_crypto_session *sess,
		const struct rte_crypto_sym_xform *auth_xform)
{
	/* Make sure we've got proper struct */
	if (auth_xform->type != RTE_CRYPTO_SYM_XFORM_AUTH) {
		MRVL_CRYPTO_LOG_ERR("Wrong xform struct provided!");
		return -EINVAL;
	}
	/* See if map data is present and valid */
	if ((auth_xform->auth.algo > RTE_DIM(auth_map)) ||
		(auth_map[auth_xform->auth.algo].supported != ALGO_SUPPORTED)) {
		MRVL_CRYPTO_LOG_ERR("Auth algorithm not supported!");
		return -EINVAL;
	}

	sess->sam_sess_params.auth_alg =
		auth_map[auth_xform->auth.algo].auth_alg;
	sess->sam_sess_params.u.basic.auth_aad_len =
		auth_xform->auth.add_auth_data_length;
	sess->sam_sess_params.u.basic.auth_icv_len = auth_xform->auth.digest_length;
	/* auth_key must be NULL if auth algorithm does not use HMAC */
	sess->sam_sess_params.auth_key = auth_xform->auth.key.length ?
					 auth_xform->auth.key.data : NULL;
	sess->sam_sess_params.auth_key_len = auth_xform->auth.key.length;

	return 0;
}

/**
 * Set session parameters (common).
 *
 * @param sess Crypto session pointer.
 * @param cipher_xform Pointer to configuration structure
 *                     for cipher operations.
 * @param auth_xform Pointer to configuration structure for auth operations.
 * @returns 0 in case of success, negative value otherwise.
 */
static int
mrvl_crypto_set_session_parameters(struct mrvl_crypto_session *sess,
		const struct rte_crypto_sym_xform *cipher_xform,
		const struct rte_crypto_sym_xform *auth_xform)
{
	/* Select cipher direction */
	if (cipher_xform != NULL) {
		sess->sam_sess_params.dir =
			(cipher_xform->cipher.op
				== RTE_CRYPTO_CIPHER_OP_ENCRYPT) ?
					SAM_DIR_ENCRYPT :
					SAM_DIR_DECRYPT;
	} else if (auth_xform != NULL) {
		sess->sam_sess_params.dir =
			(auth_xform->auth.op
				== RTE_CRYPTO_AUTH_OP_GENERATE) ?
					SAM_DIR_ENCRYPT :
					SAM_DIR_DECRYPT;
	} else {
		/* Having empty both cipher and algo is definitely an error */
		return -EINVAL;
	}

	if ((cipher_xform != NULL) &&
		(mrvl_crypto_set_cipher_session_parameters(
			sess, cipher_xform) < 0)) {
		return -EINVAL;
	}

	if ((auth_xform != NULL) &&
		(mrvl_crypto_set_auth_session_parameters(
			sess, auth_xform) < 0)) {
		return -EINVAL;
	}

	/*
	 * GMAC in DPDK is configured as cipher-GCM/auth-GMAC, MUSDK requires
	 * explicit setting of cipher-GMAC
	 */
	if ((sess->sam_sess_params.cipher_mode == SAM_CIPHER_GCM) &&
		(sess->sam_sess_params.auth_alg == SAM_AUTH_AES_GMAC)) {
		sess->sam_sess_params.cipher_mode = SAM_CIPHER_GMAC;
	}
	return 0;
}

/**
 * Parse crypto transform chain and setup session parameters.
 *
 * @param dev Pointer to crypto device
 * @param sess Poiner to crypto session
 * @param xform Pointer to configuration structure chain for crypto operations.
 * @returns 0 in case of success, negative value otherwise.
 */
int
mrvl_crypto_prepare_session_parameters(struct rte_cryptodev *dev,
		struct mrvl_crypto_session *sess,
		const struct rte_crypto_sym_xform *xform)
{
	const struct rte_crypto_sym_xform *cipher_xform = NULL;
	const struct rte_crypto_sym_xform *auth_xform = NULL;
	enum mrvl_crypto_chain_order chain_order;
	int ret;

	/* Filter out spurious/broken requests */
	if (xform == NULL)
		return -EINVAL;

	sess->dev = dev;

	chain_order = mrvl_crypto_get_chain_order(xform);
	switch (chain_order) {
	case MRVL_CRYPTO_CHAIN_CIPHER_AUTH:
		cipher_xform = xform;
		auth_xform = xform->next;
		break;
	case MRVL_CRYPTO_CHAIN_AUTH_CIPHER:
		auth_xform = xform;
		cipher_xform = xform->next;
		break;
	case MRVL_CRYPTO_CHAIN_CIPHER_ONLY:
		cipher_xform = xform;
		if (cipher_xform->cipher.algo == RTE_CRYPTO_CIPHER_AES_GCM) {
			MRVL_CRYPTO_LOG_ERR(
					"AES GCM/GMAC requires configured authentication!");
			return -EINVAL;
		}
		break;
	case MRVL_CRYPTO_CHAIN_AUTH_ONLY:
		auth_xform = xform;
		if ((auth_xform->auth.algo == RTE_CRYPTO_AUTH_AES_GCM) ||
			(auth_xform->auth.algo == RTE_CRYPTO_AUTH_AES_GMAC)) {
			MRVL_CRYPTO_LOG_ERR(
					"AES GCM/GMAC requires configured cipher!");
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}

	ret = mrvl_crypto_set_session_parameters(
			sess, cipher_xform, auth_xform);
	if (unlikely(ret != 0)) {
		MRVL_CRYPTO_LOG_ERR(
		"Invalid/unsupported (cipher/auth) parameters");
		return -EINVAL;
	}
	sess->state = MRVL_SESSION_CONFIGURED;

	return 0;
}

/*
 *-----------------------------------------------------------------------------
 * Process Operations
 *-----------------------------------------------------------------------------
 */

/**
 * Make sure the session is started.
 *
 * The function makes sure the session state is correct and session itself
 * is started. If session is not started yet, it tries to start it.
 * @param op Pointer to DPDK crypto operation struct.
 * @returns 0 in case of success, negative value otherwise.
 */
static inline int
mrvl_make_sure_session_started(struct rte_crypto_op *op)
{
	struct mrvl_crypto_session *session =
		(struct mrvl_crypto_session *)op->sym->session->_private;

	if (session->state == MRVL_SESSION_CONFIGURED) {
		/* Need to start session first */
		if (sam_session_create(&session->sam_sess_params,
				&session->sam_sess)) {
			/*
			 * We're using Dbg here to make sure
			 * the function is inlined.
			 */
			MRVL_CRYPTO_LOG_DBG("Failed to start session!");
			return -EIO;
		}
		session->state = MRVL_SESSION_STARTED;
	} else if (session->state != MRVL_SESSION_STARTED) {
		MRVL_CRYPTO_LOG_DBG(
			"Invalid session state (%d)!", session->state);
		return -EINVAL;
	}
	return 0;
}

/**
 * Prepare a single request.
 *
 * This function basically translates DPDK crypto request into one
 * understandable by MUDSK's SAM. If this is a first request in a session,
 * it starts the session.
 *
 * @param request Pointer to pre-allocated && reset request buffer [Out].
 * @param src_bd Pointer to pre-allocated source descriptor [Out].
 * @param dst_bd Pointer to pre-allocated destination descriptor [Out].
 * @param op Pointer to DPDK crypto operation struct [In].
 */
static inline int
mrvl_request_prepare(struct sam_cio_op_params *request,
		struct sam_buf_info *src_bd,
		struct sam_buf_info *dst_bd,
		struct rte_crypto_op *op)
{
	struct mrvl_crypto_session *session =
		(struct mrvl_crypto_session *) op->sym->session->_private;
	int auth_data_len;
	struct rte_mbuf *dst_mbuf;

	/*
	 * If application delivered us null dst buffer, it means it expects
	 * us to deliver the result in src buffer.
	 */
	dst_mbuf = op->sym->m_dst ? op->sym->m_dst : op->sym->m_src;

	request->sa = session->sam_sess;
	request->cookie = op;

	/* Single buffers only, sorry. */
	request->num_bufs = 1;
	request->src = src_bd;
	src_bd->vaddr = rte_pktmbuf_mtod(op->sym->m_src, void *);
	src_bd->paddr = rte_pktmbuf_mtophys(op->sym->m_src);
	src_bd->len = rte_pktmbuf_data_len(op->sym->m_src);

	/* Empty source. */
	if (rte_pktmbuf_data_len(op->sym->m_src) == 0) {
		/* EIP does not support 0 length buffers. */
		MRVL_CRYPTO_LOG_ERR("Buffer length == 0 not supported!");
		return -1;
	}

	/* Empty destination. */
	if (rte_pktmbuf_data_len(dst_mbuf) == 0) {
		/* Make dst buffer fit at least source data. */
		if (rte_pktmbuf_append(dst_mbuf,
			rte_pktmbuf_data_len(op->sym->m_src)) == NULL) {
			MRVL_CRYPTO_LOG_ERR("Unable to set big enough dst buffer!");
			return -1;
		}
	}

	request->dst = dst_bd;
	dst_bd->vaddr = rte_pktmbuf_mtod(dst_mbuf, void *);
	dst_bd->paddr = rte_pktmbuf_mtophys(dst_mbuf);

	/*
	 * We can use all available space in dst_mbuf,
	 * not only what's used currently.
	 */
	dst_bd->len = dst_mbuf->buf_len - rte_pktmbuf_headroom(dst_mbuf);

	request->cipher_len = op->sym->cipher.data.length;
	request->cipher_offset = op->sym->cipher.data.offset;
	request->cipher_iv = op->sym->cipher.iv.data;
	request->cipher_iv_offset = 0;

	request->auth_len = op->sym->auth.data.length;
	request->auth_offset = op->sym->auth.data.offset;
	request->auth_aad = op->sym->auth.aad.data;
	request->auth_aad_offset = 0;

	if (op->sym->auth.digest.data == NULL) {
		/* No auth - no worry. */
		return 0;
	}

	auth_data_len = op->sym->auth.data.length + op->sym->auth.data.offset;

	/*
	 * EIP supports only scenarios where ICV(digest buffer) is placed at
	 * auth.data.offset + auth.data.length.
	 * Any other placement means risking errors.
	 */
	if (session->sam_sess_params.dir == SAM_DIR_ENCRYPT) {
		/*
		 * This should be the most common case anyway,
		 * EIP will overwrite DST buffer at auth_data_len offset.
		 */
		if (rte_pktmbuf_mtod_offset(
				dst_mbuf, uint8_t *, auth_data_len) ==
			op->sym->auth.digest.data) {
			return 0;
		}
	} else {/* session->sam_sess_params.dir == SAM_DIR_DECRYPT */
		/*
		 * EIP will look for digest at auth_data_len offset in SRC buffer.
		 */
		if (rte_pktmbuf_mtod_offset(
				op->sym->m_src, uint8_t *, auth_data_len) ==
			op->sym->auth.digest.data) {
			return 0;
		}
	}

	/*
	 * If we landed here it means that digest pointer is
	 * at different than expected place.
	 */
	return -1;
}
/*
 *-----------------------------------------------------------------------------
 * PMD Framework handlers
 *-----------------------------------------------------------------------------
 */

/**
 * Enqueue burst.
 *
 * @param queue_pair Pointer to queue pair.
 * @param ops Pointer to ops requests array.
 * @param nb_ops Number of elements in ops requests array.
 * @returns Number of elements consumed from ops.
 */
static uint16_t
mrvl_crypto_pmd_enqueue_burst(void *queue_pair,
		struct rte_crypto_op **ops,
		uint16_t nb_ops)
{
	uint16_t iter_ops = 0;
	uint16_t to_enq = nb_ops;
	uint16_t consumed = 0;
	uint16_t iter_req = 0;
	int ret;
	struct sam_cio_op_params requests[nb_ops];
	/*
	 * DPDK uses single fragment buffers, so we can KISS descriptors.
	 * SAM does not store bd pointers, so on-stack scope will be enough.
	 */
	struct sam_buf_info src_bd[nb_ops];
	struct sam_buf_info dst_bd[nb_ops];
	struct mrvl_crypto_qp *qp = (struct mrvl_crypto_qp *) queue_pair;

	if (nb_ops == 0)
		return 0;

	/* Prepare the burst. */
	memset(&requests, 0, sizeof(requests));

	/* Iterate through */
	for (; iter_ops < nb_ops; ++iter_ops, ++iter_req) {
		if ((mrvl_make_sure_session_started(ops[iter_ops]) < 0) ||
			(mrvl_request_prepare(
				&requests[iter_req],
				&src_bd[iter_req],
				&dst_bd[iter_req],
				ops[iter_ops]) < 0)) {
			MRVL_CRYPTO_LOG_ERR(
				"Error while parameters preparation!");
			qp->stats.enqueue_err_count++;
			ops[iter_ops]->status = RTE_CRYPTO_OP_STATUS_ERROR;
			/* Rollback index to reuse request slot. */
			--iter_req;

			/* Decrease the number of ops to enqueue. */
			--to_enq;

			/*
			 * Number of handled ops is increased
			 * (even if the result of handling is error).
			 */
			++consumed;
		} else {
			/* Assume enqueue will succeed. */
			ops[iter_ops]->status = RTE_CRYPTO_OP_STATUS_ENQUEUED;
		}
	} /* for (; iter_ops < nb_ops;... */

	if (to_enq > 0) {
		/* Send the burst */
		ret = sam_cio_enq(qp->cio, requests, &to_enq);
		consumed += to_enq;
		if (ret < 0) {
			/*
			 * Trust SAM that in this case returned value will be at
			 * some point correct (now it is returned unmodified).
			 */
			qp->stats.enqueue_err_count += to_enq;
			for (iter_ops = 0; iter_ops < to_enq; ++iter_ops)
				ops[iter_ops]->status = RTE_CRYPTO_OP_STATUS_ERROR;
		}
	}

	if (consumed < nb_ops) {
		/*
		 * No room to send more.
		 * Correct the state of the rest of requests.
		 */
		for (iter_ops = consumed; iter_ops < nb_ops; ++iter_ops) {
			if (ops[iter_ops]->status ==
					RTE_CRYPTO_OP_STATUS_ENQUEUED)
				ops[iter_ops]->status =
					RTE_CRYPTO_OP_STATUS_NOT_PROCESSED;
				/* Leave out errors. */
		}
	}

	qp->stats.enqueued_count += consumed;
	return consumed;
}

/**
 * Dequeue burst.
 *
 * @param queue_pair Pointer to queue pair.
 * @param ops Pointer to ops requests array.
 * @param nb_ops Number of elements in ops requests array.
 * @returns Number of elements dequeued.
 */
static uint16_t
mrvl_crypto_pmd_dequeue_burst(void *queue_pair,
		struct rte_crypto_op **ops,
		uint16_t nb_ops)
{
	int ret;
	struct mrvl_crypto_qp *qp = queue_pair;
	struct sam_cio *cio = qp->cio;
	struct sam_cio_op_result results[nb_ops];
	uint16_t i;

	ret = sam_cio_deq(cio, results, &nb_ops);
	if (ret < 0) {
		/* Count all dequeued as error. */
		qp->stats.dequeue_err_count += nb_ops;

		/* But act as they were dequeued anyway*/
		qp->stats.dequeued_count += nb_ops;

		return 0;
	}

	/* Unpack and check results. */
	for (i = 0; i < nb_ops; ++i) {
		ops[i] = results[i].cookie;

		switch (results[i].status) {
		case SAM_CIO_OK:
			ops[i]->status = RTE_CRYPTO_OP_STATUS_SUCCESS;
			break;
		case SAM_CIO_ERR_ICV:
			MRVL_CRYPTO_LOG_DBG("CIO returned SAM_CIO_ERR_ICV.");
			ops[i]->status = RTE_CRYPTO_OP_STATUS_AUTH_FAILED;
			break;
		default:
			MRVL_CRYPTO_LOG_DBG(
				"CIO returned Error: %d", results[i].status);
			ops[i]->status = RTE_CRYPTO_OP_STATUS_ERROR;
			break;
		}
	}

	qp->stats.dequeued_count += nb_ops;
	return nb_ops;
}

/**
 * Create a new crypto device.
 *
 * @param name Driver name.
 * @param vdev Pointer to device structure.
 * @param init_params Pointer to initialization parameters.
 * @returns 0 in case of success, negative value otherwise.
 */
static int
cryptodev_mrvl_crypto_create(const char *name,
		struct rte_vdev_device *vdev,
		struct rte_crypto_vdev_init_params *init_params)
{
	struct rte_cryptodev *dev;
	struct mrvl_crypto_private *internals;
	struct sam_init_params	sam_params;
	int ret;

	if (init_params->name[0] == '\0') {
		ret = rte_cryptodev_pmd_create_dev_name(
				init_params->name, name);

		if (ret < 0) {
			MRVL_CRYPTO_LOG_ERR("failed to create unique name");
			return ret;
		}
	}

	dev = rte_cryptodev_pmd_virtual_dev_init(init_params->name,
				sizeof(struct mrvl_crypto_private),
				init_params->socket_id);
	if (dev == NULL) {
		MRVL_CRYPTO_LOG_ERR("failed to create cryptodev vdev");
		goto init_error;
	}

	dev->dev_type = RTE_CRYPTODEV_MRVL_PMD;
	dev->dev_ops = &mrvl_crypto_pmd_ops;

	/* Register rx/tx burst functions for data path. */
	dev->enqueue_burst = mrvl_crypto_pmd_enqueue_burst;
	dev->dequeue_burst = mrvl_crypto_pmd_dequeue_burst;

	dev->feature_flags = RTE_CRYPTODEV_FF_SYMMETRIC_CRYPTO |
			RTE_CRYPTODEV_FF_SYM_OPERATION_CHAINING |
			RTE_CRYPTODEV_FF_HW_ACCELERATED;

	/* Set vector instructions mode supported */
	internals = dev->data->dev_private;

	internals->max_nb_qpairs = init_params->max_nb_queue_pairs;
	internals->max_nb_sessions = init_params->max_nb_sessions;

	/*
	 * ret == -EEXIST is correct, it means DMA
	 * has been already initialized.
	 */
	ret = mv_sys_dma_mem_init(RTE_MRVL_MUSDK_DMA_MEMSIZE);
	if ((ret < 0) && (ret != -EEXIST))
		return ret;

	sam_params.max_num_sessions = internals->max_nb_sessions;

	return sam_init(&sam_params);

init_error:
	MRVL_CRYPTO_LOG_ERR(
		"driver %s: cryptodev_mrvl_crypto_create failed",
		init_params->name);

	cryptodev_mrvl_crypto_uninit(vdev);
	return -EFAULT;
}

/**
 * Initialize the crypto device.
 *
 * @param vdev Pointer to device structure.
 * @returns 0 in case of success, negative value otherwise.
 */
static int
cryptodev_mrvl_crypto_init(struct rte_vdev_device *vdev)
{
	struct rte_crypto_vdev_init_params init_params = { };
	const char *name;
	const char *input_args;
	int ret;

	name = rte_vdev_device_name(vdev);
	if (name == NULL)
		return -EINVAL;
	input_args = rte_vdev_device_args(vdev);

	if (!input_args)
		return -EINVAL;

	init_params.max_nb_queue_pairs = sam_get_num_inst() * SAM_HW_RING_NUM;
	init_params.max_nb_sessions = RTE_CRYPTODEV_VDEV_DEFAULT_MAX_NB_SESSIONS;
	init_params.socket_id = rte_socket_id();

	ret = rte_cryptodev_parse_vdev_init_params(&init_params, input_args);
	if (ret) {
		RTE_LOG(ERR, PMD, "Failed to parse input arguments\n");
		return ret;
	}

	RTE_LOG(INFO, PMD, "Initialising %s on NUMA node %d\n", name,
			init_params.socket_id);
	if (init_params.name[0] != '\0') {
		RTE_LOG(INFO, PMD, "  User defined name = %s\n",
			init_params.name);
	}
	RTE_LOG(INFO, PMD, "  Max number of queue pairs = %d\n",
			init_params.max_nb_queue_pairs);
	RTE_LOG(INFO, PMD, "  Max number of sessions = %d\n",
			init_params.max_nb_sessions);

	return cryptodev_mrvl_crypto_create(name, vdev, &init_params);
}

/**
 * Uninitialize the crypto device
 *
 * @param vdev Pointer to device structure.
 * @returns 0 in case of success, negative value otherwise.
 */
static int
cryptodev_mrvl_crypto_uninit(struct rte_vdev_device *vdev)
{
	const char *name = rte_vdev_device_name(vdev);

	if (name == NULL)
		return -EINVAL;

	RTE_LOG(INFO, PMD,
		"Closing Marvell crypto device %s on numa socket %u\n",
		name, rte_socket_id());

	return 0;
}

/**
 * Basic driver handlers for use in the constructor.
 */
static struct rte_vdev_driver mrvl_crypto_drv = {
	.probe = cryptodev_mrvl_crypto_init,
	.remove = cryptodev_mrvl_crypto_uninit
};

/* Register the driver in constructor. */
RTE_PMD_REGISTER_VDEV(CRYPTODEV_NAME_MRVL_PMD, mrvl_crypto_drv);
RTE_PMD_REGISTER_ALIAS(CRYPTODEV_NAME_MRVL_PMD, crypto_mrvl_pmd);
RTE_PMD_REGISTER_PARAM_STRING(CRYPTODEV_NAME_MRVL_PMD,
	"max_nb_queue_pairs=<int> "
	"max_nb_sessions=<int> "
	"socket_id=<int>");
