#pragma once

#include <cstdint>

// Minimal declarations for the Bluedroid L2CAP APIs used by the PS3 BD
// remote HID transport. These are private ESP-IDF interfaces, so keep this
// file aligned with the Bluedroid version shipped by the Arduino ESP32 core.

using UINT8 = uint8_t;
using UINT16 = uint16_t;
using UINT32 = uint32_t;
using BOOLEAN = bool;
using BD_ADDR = uint8_t[6];

struct BT_HDR {
    uint16_t event;
    uint16_t len;
    uint16_t offset;
    uint16_t layer_specific;
    uint8_t data[];
};

struct FLOW_SPEC {
    UINT8 qos_flags;
    UINT8 service_type;
    UINT32 token_rate;
    UINT32 token_bucket_size;
    UINT32 peak_bandwidth;
    UINT32 latency;
    UINT32 delay_variation;
};

struct tHCI_EXT_FLOW_SPEC {
    UINT8 id;
    UINT8 stype;
    UINT16 max_sdu_size;
    UINT32 sdu_inter_time;
    UINT32 access_latency;
    UINT32 flush_timeout;
};

struct tL2CAP_FCR_OPTS {
    UINT8 mode;
    UINT8 tx_win_sz;
    UINT8 max_transmit;
    UINT16 rtrans_tout;
    UINT16 mon_tout;
    UINT16 mps;
};

struct tL2CAP_CFG_INFO {
    UINT16 result;
    BOOLEAN mtu_present;
    UINT16 mtu;
    BOOLEAN qos_present;
    FLOW_SPEC qos;
    BOOLEAN flush_to_present;
    UINT16 flush_to;
    BOOLEAN fcr_present;
    tL2CAP_FCR_OPTS fcr;
    BOOLEAN fcs_present;
    UINT8 fcs;
    BOOLEAN ext_flow_spec_present;
    tHCI_EXT_FLOW_SPEC ext_flow_spec;
    UINT16 flags;
};

struct tL2CAP_ERTM_INFO {
    UINT8 preferred_mode;
    UINT8 allowed_modes;
    UINT16 user_rx_buf_size;
    UINT16 user_tx_buf_size;
    UINT16 fcr_rx_buf_size;
    UINT16 fcr_tx_buf_size;
};

using tL2CA_CONNECT_IND_CB = void(BD_ADDR, UINT16, UINT16, UINT8);
using tL2CA_CONNECT_CFM_CB = void(UINT16, UINT16);
using tL2CA_CONNECT_PND_CB = void(UINT16);
using tL2CA_CONFIG_IND_CB = void(UINT16, tL2CAP_CFG_INFO*);
using tL2CA_CONFIG_CFM_CB = void(UINT16, tL2CAP_CFG_INFO*);
using tL2CA_DISCONNECT_IND_CB = void(UINT16, BOOLEAN);
using tL2CA_DISCONNECT_CFM_CB = void(UINT16, UINT16);
using tL2CA_QOS_VIOLATION_IND_CB = void(BD_ADDR);
using tL2CA_DATA_IND_CB = void(UINT16, BT_HDR*);
using tL2CA_CONGESTION_STATUS_CB = void(UINT16, BOOLEAN);
using tL2CA_TX_COMPLETE_CB = void(UINT16, UINT16);

struct tL2CAP_APPL_INFO {
    tL2CA_CONNECT_IND_CB* pL2CA_ConnectInd_Cb;
    tL2CA_CONNECT_CFM_CB* pL2CA_ConnectCfm_Cb;
    tL2CA_CONNECT_PND_CB* pL2CA_ConnectPnd_Cb;
    tL2CA_CONFIG_IND_CB* pL2CA_ConfigInd_Cb;
    tL2CA_CONFIG_CFM_CB* pL2CA_ConfigCfm_Cb;
    tL2CA_DISCONNECT_IND_CB* pL2CA_DisconnectInd_Cb;
    tL2CA_DISCONNECT_CFM_CB* pL2CA_DisconnectCfm_Cb;
    tL2CA_QOS_VIOLATION_IND_CB* pL2CA_QoSViolationInd_Cb;
    tL2CA_DATA_IND_CB* pL2CA_DataInd_Cb;
    tL2CA_CONGESTION_STATUS_CB* pL2CA_CongestionStatus_Cb;
    tL2CA_TX_COMPLETE_CB* pL2CA_TxComplete_Cb;
};

constexpr UINT16 kL2capPsmsHidControl = 0x0011;
constexpr UINT16 kL2capPsmsHidInterrupt = 0x0013;
constexpr UINT16 kL2capConnectOk = 0;
constexpr UINT16 kL2capConnectPending = 1;
constexpr UINT16 kL2capConfigOk = 0;
constexpr UINT16 kL2capConnectPendingStatus = 1;

constexpr UINT8 kSecServiceFirstEmpty = 54;
constexpr UINT16 kSecNone = 0;

extern "C" {

UINT16 L2CA_Register(UINT16 psm, tL2CAP_APPL_INFO* callbacks);
void L2CA_Deregister(UINT16 psm);
BOOLEAN L2CA_ErtmConnectRsp(BD_ADDR bd_addr, UINT8 id, UINT16 lcid,
                            UINT16 result, UINT16 status,
                            tL2CAP_ERTM_INFO* ertm_info);
BOOLEAN L2CA_ConfigReq(UINT16 cid, tL2CAP_CFG_INFO* config);
BOOLEAN L2CA_ConfigRsp(UINT16 cid, tL2CAP_CFG_INFO* config);
BOOLEAN L2CA_DisconnectRsp(UINT16 cid);
BOOLEAN L2CA_DisconnectReq(UINT16 cid);
BOOLEAN BTM_SetSecurityLevel(BOOLEAN is_originator, const char* name,
                             UINT8 service_id, UINT16 sec_level,
                             UINT16 psm, UINT32 mx_proto_id,
                             UINT32 mx_chan_id);

}

