/*
 * Licensed to the OpenAirInterface (OAI) Software Alliance under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The OpenAirInterface Software Alliance licenses this file to You under
 * the OAI Public License, Version 1.1  (the "License"); you may not use this file
 * except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.openairinterface.org/?page_id=698
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *-------------------------------------------------------------------------------
 * For more information about the OpenAirInterface (OAI) Software Alliance:
 *      contact@openairinterface.org
 */

/*! \file gNB_scheduler_ulsch.c
 * \brief gNB procedures for the ULSCH transport channel
 * \author Navid Nikaein and Raymond Knopp, Guido Casati
 * \date 2019
 * \email: guido.casati@iis.fraunhofer.de
 * \version 1.0
 * @ingroup _mac
 */


#include "LAYER2/NR_MAC_gNB/mac_proto.h"
#include "executables/softmodem-common.h"
#include "common/utils/nr/nr_common.h"

//38.321 Table 6.1.3.1-1
const uint32_t NR_SHORT_BSR_TABLE[32] = {
    0,    10,    14,    20,    28,     38,     53,     74,
  102,   142,   198,   276,   384,    535,    745,   1038,
 1446,  2014,  2806,  3909,  5446,   7587,  10570,  14726,
20516, 28581, 39818, 55474, 77284, 107669, 150000, 300000
};

//38.321 Table 6.1.3.1-2
const uint32_t NR_LONG_BSR_TABLE[256] ={
       0,       10,       11,       12,       13,       14,       15,       16,       17,       18,       19,       20,       22,       23,        25,         26,
      28,       30,       32,       34,       36,       38,       40,       43,       46,       49,       52,       55,       59,       62,        66,         71,
      75,       80,       85,       91,       97,      103,      110,      117,      124,      132,      141,      150,      160,      170,       181,        193,
     205,      218,      233,      248,      264,      281,      299,      318,      339,      361,      384,      409,      436,      464,       494,        526,
     560,      597,      635,      677,      720,      767,      817,      870,      926,      987,     1051,     1119,     1191,     1269,      1351,       1439,
    1532,     1631,     1737,     1850,     1970,     2098,     2234,     2379,     2533,     2698,     2873,     3059,     3258,     3469,      3694,       3934,
    4189,     4461,     4751,     5059,     5387,     5737,     6109,     6506,     6928,     7378,     7857,     8367,     8910,     9488,     10104,      10760,
   11458,    12202,    12994,    13838,    14736,    15692,    16711,    17795,    18951,    20181,    21491,    22885,    24371,    25953,     27638,      29431,
   31342,    33376,    35543,    37850,    40307,    42923,    45709,    48676,    51836,    55200,    58784,    62599,    66663,    70990,     75598,      80505,
   85730,    91295,    97221,   103532,   110252,   117409,   125030,   133146,   141789,   150992,   160793,   171231,   182345,   194182,    206786,     220209,
  234503,   249725,   265935,   283197,   301579,   321155,   342002,   364202,   387842,   413018,   439827,   468377,   498780,   531156,    565634,     602350,
  641449,   683087,   727427,   774645,   824928,   878475,   935498,   996222,  1060888,  1129752,  1203085,  1281179,  1364342,  1452903,   1547213,    1647644,
 1754595,  1868488,  1989774,  2118933,  2256475,  2402946,  2558924,  2725027,  2901912,  3090279,  3290873,  3504487,  3731968,  3974215,   4232186,    4506902,
 4799451,  5110989,  5442750,  5796046,  6172275,  6572925,  6999582,  7453933,  7937777,  8453028,  9001725,  9586039, 10208280, 10870913,  11576557,   12328006,
13128233, 13980403, 14887889, 15854280, 16883401, 17979324, 19146385, 20389201, 21712690, 23122088, 24622972, 26221280, 27923336, 29735875,  31666069,   33721553,
35910462, 38241455, 40723756, 43367187, 46182206, 49179951, 52372284, 55771835, 59392055, 63247269, 67352729, 71724679, 76380419, 81338368, 162676736, 4294967295
};

void nr_process_mac_pdu(
    module_id_t module_idP,
    rnti_t rnti,
    uint8_t CC_id,
    frame_t frameP,
    uint8_t *pduP,
    uint16_t mac_pdu_len)
{

    // This function is adapting code from the old
    // parse_header(...) and ue_send_sdu(...) functions of OAI LTE

    uint8_t *pdu_ptr = pduP, rx_lcid, done = 0;
    int pdu_len = mac_pdu_len;
    uint16_t mac_ce_len, mac_subheader_len, mac_sdu_len;


    NR_UE_info_t *UE_info = &RC.nrmac[module_idP]->UE_info;
    int UE_id = find_nr_UE_id(module_idP, rnti);
    if (UE_id == -1) {
      LOG_E(MAC, "%s() UE_id == -1\n",__func__);
      return;
    }
    NR_UE_sched_ctrl_t *sched_ctrl = &UE_info->UE_sched_ctrl[UE_id];
    //  For both DL/UL-SCH
    //  Except:
    //   - UL/DL-SCH: fixed-size MAC CE(known by LCID)
    //   - UL/DL-SCH: padding
    //   - UL-SCH:    MSG3 48-bits
    //  |0|1|2|3|4|5|6|7|  bit-wise
    //  |R|F|   LCID    |
    //  |       L       |
    //  |0|1|2|3|4|5|6|7|  bit-wise
    //  |R|F|   LCID    |
    //  |       L       |
    //  |       L       |

    //  For both DL/UL-SCH
    //  For:
    //   - UL/DL-SCH: fixed-size MAC CE(known by LCID)
    //   - UL/DL-SCH: padding, for single/multiple 1-oct padding CE(s)
    //   - UL-SCH:    MSG3 48-bits
    //  |0|1|2|3|4|5|6|7|  bit-wise
    //  |R|R|   LCID    |
    //  LCID: The Logical Channel ID field identifies the logical channel instance of the corresponding MAC SDU or the type of the corresponding MAC CE or padding as described in Tables 6.2.1-1 and 6.2.1-2 for the DL-SCH and UL-SCH respectively. There is one LCID field per MAC subheader. The LCID field size is 6 bits;
    //  L: The Length field indicates the length of the corresponding MAC SDU or variable-sized MAC CE in bytes. There is one L field per MAC subheader except for subheaders corresponding to fixed-sized MAC CEs and padding. The size of the L field is indicated by the F field;
    //  F: lenght of L is 0:8 or 1:16 bits wide
    //  R: Reserved bit, set to zero.

    while (!done && pdu_len > 0){
        mac_ce_len = 0;
        mac_subheader_len = 1; //  default to fixed-length subheader = 1-oct
        mac_sdu_len = 0;
        rx_lcid = ((NR_MAC_SUBHEADER_FIXED *)pdu_ptr)->LCID;

        LOG_D(MAC, "LCID received at gNB side: %d \n", rx_lcid);

        unsigned char *ce_ptr;
        int n_Lcg = 0;

        switch(rx_lcid){
            //  MAC CE

            /*#ifdef DEBUG_HEADER_PARSING
              LOG_D(MAC, "[UE] LCID %d, PDU length %d\n", ((NR_MAC_SUBHEADER_FIXED *)pdu_ptr)->LCID, pdu_len);
            #endif*/
        case UL_SCH_LCID_RECOMMENDED_BITRATE_QUERY:
              // 38.321 Ch6.1.3.20
              mac_ce_len = 2;
              break;
        case UL_SCH_LCID_CONFIGURED_GRANT_CONFIRMATION:
                // 38.321 Ch6.1.3.7
                break;

        case UL_SCH_LCID_S_BSR:
        case UL_SCH_LCID_S_TRUNCATED_BSR:
        	//38.321 section 6.1.3.1
        	//fixed length
        	mac_ce_len =1;
        	/* Extract short BSR value */
               ce_ptr = &pdu_ptr[mac_subheader_len];
               NR_BSR_SHORT *bsr_s = (NR_BSR_SHORT *) ce_ptr;
               sched_ctrl->estimated_ul_buffer = 0;
               sched_ctrl->estimated_ul_buffer = NR_SHORT_BSR_TABLE[bsr_s->Buffer_size];
               LOG_D(MAC, "SHORT BSR, LCG ID %d, BS Index %d, BS value < %d, est buf %d\n",
                     bsr_s->LcgID,
                     bsr_s->Buffer_size,
                     NR_SHORT_BSR_TABLE[bsr_s->Buffer_size],
                     sched_ctrl->estimated_ul_buffer);
        	break;

        case UL_SCH_LCID_L_BSR:
        case UL_SCH_LCID_L_TRUNCATED_BSR:
        	//38.321 section 6.1.3.1
        	//variable length
        	mac_ce_len |= (uint16_t)((NR_MAC_SUBHEADER_SHORT *)pdu_ptr)->L;
        	mac_subheader_len = 2;
        	if(((NR_MAC_SUBHEADER_SHORT *)pdu_ptr)->F){
        		mac_ce_len |= (uint16_t)(((NR_MAC_SUBHEADER_LONG *)pdu_ptr)->L2)<<8;
        		mac_subheader_len = 3;
        	}
        	/* Extract long BSR value */
               ce_ptr = &pdu_ptr[mac_subheader_len];
               NR_BSR_LONG *bsr_l = (NR_BSR_LONG *) ce_ptr;
               sched_ctrl->estimated_ul_buffer = 0;

               n_Lcg = bsr_l->LcgID7 + bsr_l->LcgID6 + bsr_l->LcgID5 + bsr_l->LcgID4 +
                       bsr_l->LcgID3 + bsr_l->LcgID2 + bsr_l->LcgID1 + bsr_l->LcgID0;

               LOG_D(MAC, "LONG BSR, LCG ID(7-0) %d/%d/%d/%d/%d/%d/%d/%d\n",
                     bsr_l->LcgID7, bsr_l->LcgID6, bsr_l->LcgID5, bsr_l->LcgID4,
                     bsr_l->LcgID3, bsr_l->LcgID2, bsr_l->LcgID1, bsr_l->LcgID0);

               for (int n = 0; n < n_Lcg; n++){
                 LOG_D(MAC, "LONG BSR, %d/%d (n/n_Lcg), BS Index %d, BS value < %d",
                       n, n_Lcg, pdu_ptr[mac_subheader_len + 1 + n],
                       NR_LONG_BSR_TABLE[pdu_ptr[mac_subheader_len + 1 + n]]);
                 sched_ctrl->estimated_ul_buffer +=
                       NR_LONG_BSR_TABLE[pdu_ptr[mac_subheader_len + 1 + n]];
               }

        	break;

        case UL_SCH_LCID_C_RNTI:
        	//38.321 section 6.1.3.2
        	//fixed length
        	mac_ce_len = 2;
        	/* Extract CRNTI value */
        	break;

        case UL_SCH_LCID_SINGLE_ENTRY_PHR:
        	//38.321 section 6.1.3.8
        	//fixed length
        	mac_ce_len = 2;
        	/* Extract SINGLE ENTRY PHR elements for PHR calculation */
        	break;

        case UL_SCH_LCID_MULTI_ENTRY_PHR_1_OCT:
        	//38.321 section 6.1.3.9
        	//  varialbe length
        	mac_ce_len |= (uint16_t)((NR_MAC_SUBHEADER_SHORT *)pdu_ptr)->L;
        	mac_subheader_len = 2;
        	if(((NR_MAC_SUBHEADER_SHORT *)pdu_ptr)->F){
        		mac_ce_len |= (uint16_t)(((NR_MAC_SUBHEADER_LONG *)pdu_ptr)->L2)<<8;
        		mac_subheader_len = 3;
        	}
        	/* Extract MULTI ENTRY PHR elements from single octet bitmap for PHR calculation */
        	break;

        case UL_SCH_LCID_MULTI_ENTRY_PHR_4_OCT:
        	//38.321 section 6.1.3.9
        	//  varialbe length
        	mac_ce_len |= (uint16_t)((NR_MAC_SUBHEADER_SHORT *)pdu_ptr)->L;
        	mac_subheader_len = 2;
        	if(((NR_MAC_SUBHEADER_SHORT *)pdu_ptr)->F){
        		mac_ce_len |= (uint16_t)(((NR_MAC_SUBHEADER_LONG *)pdu_ptr)->L2)<<8;
        		mac_subheader_len = 3;
        	}
        	/* Extract MULTI ENTRY PHR elements from four octets bitmap for PHR calculation */
        	break;

        case UL_SCH_LCID_PADDING:
        	done = 1;
        	//  end of MAC PDU, can ignore the rest.
        	break;

        // MAC SDUs
        case UL_SCH_LCID_SRB1:
              // todo
              break;
        case UL_SCH_LCID_SRB2:
              // todo
              break;
        case UL_SCH_LCID_SRB3:
              // todo
              break;
        case UL_SCH_LCID_CCCH_MSG3:
              // todo
              break;
        case UL_SCH_LCID_CCCH:
              // todo
              mac_subheader_len = 2;
              break;

        case UL_SCH_LCID_DTCH:
                //  check if LCID is valid at current time.
                if(((NR_MAC_SUBHEADER_SHORT *)pdu_ptr)->F){
                    //mac_sdu_len |= (uint16_t)(((NR_MAC_SUBHEADER_LONG *)pdu_ptr)->L2)<<8;
                    mac_subheader_len = 3;
                    mac_sdu_len = ((uint16_t)(((NR_MAC_SUBHEADER_LONG *) pdu_ptr)->L1 & 0x7f) << 8)
                    | ((uint16_t)((NR_MAC_SUBHEADER_LONG *) pdu_ptr)->L2 & 0xff);

                } else {
                  mac_sdu_len = (uint16_t)((NR_MAC_SUBHEADER_SHORT *)pdu_ptr)->L;
                  mac_subheader_len = 2;
                }

                LOG_D(MAC, "[UE %d] Frame %d : ULSCH -> UL-DTCH %d (gNB %d, %d bytes)\n", module_idP, frameP, rx_lcid, module_idP, mac_sdu_len);
		int UE_id = find_nr_UE_id(module_idP, rnti);
		RC.nrmac[module_idP]->UE_info.mac_stats[UE_id].lc_bytes_rx[rx_lcid] += mac_sdu_len;
                #if defined(ENABLE_MAC_PAYLOAD_DEBUG)
		    log_dump(MAC, pdu_ptr + mac_subheader_len, 32, LOG_DUMP_CHAR, "\n");

                #endif
                if(IS_SOFTMODEM_NOS1){
                  mac_rlc_data_ind(module_idP,
                      0x1234,
                      module_idP,
                      frameP,
                      ENB_FLAG_YES,
                      MBMS_FLAG_NO,
                      rx_lcid,
                      (char *) (pdu_ptr + mac_subheader_len),
                      mac_sdu_len,
                      1,
                      NULL);
                }
                else{
                  mac_rlc_data_ind(module_idP,
                      rnti,
                      module_idP,
                      frameP,
                      ENB_FLAG_YES,
                      MBMS_FLAG_NO,
                      rx_lcid,
                      (char *) (pdu_ptr + mac_subheader_len),
                      mac_sdu_len,
                      1,
                      NULL);
                }

                /* Updated estimated buffer when receiving data */
                if (sched_ctrl->estimated_ul_buffer >= mac_sdu_len)
                  sched_ctrl->estimated_ul_buffer -= mac_sdu_len;
                else
                  sched_ctrl->estimated_ul_buffer = 0;

            break;

        default:
          LOG_E(MAC, "Received unknown MAC header (LCID = 0x%02x)\n", rx_lcid);
          return;
          break;
        }
        pdu_ptr += ( mac_subheader_len + mac_ce_len + mac_sdu_len );
        pdu_len -= ( mac_subheader_len + mac_ce_len + mac_sdu_len );

        if (pdu_len < 0) {
          LOG_E(MAC, "%s() residual mac pdu length < 0!, pdu_len: %d\n", __func__, pdu_len);
          LOG_E(MAC, "MAC PDU ");
          for (int i = 0; i < 20; i++) // Only printf 1st - 20nd bytes
            printf("%02x ", pdu_ptr[i]);
          printf("\n");
          return;
        }
    }
}

void handle_nr_ul_harq(module_id_t mod_id,
                       frame_t frame,
                       sub_frame_t slot,
                       const nfapi_nr_crc_t *crc_pdu)
{
  int UE_id = find_nr_UE_id(mod_id, crc_pdu->rnti);
  if (UE_id < 0) {
    LOG_E(MAC, "%s(): unknown RNTI %04x in PUSCH\n", __func__, crc_pdu->rnti);
    return;
  }
  NR_UE_info_t *UE_info = &RC.nrmac[mod_id]->UE_info;
  NR_UE_sched_ctrl_t *sched_ctrl = &UE_info->UE_sched_ctrl[UE_id];

  int max_harq_rounds = 4; // TODO define macro
  uint8_t hrq_id = crc_pdu->harq_id;
  NR_UE_ul_harq_t *cur_harq = &sched_ctrl->ul_harq_processes[hrq_id];
  if (cur_harq->state==ACTIVE_SCHED) {
    if (!crc_pdu->tb_crc_status) {
      cur_harq->ndi ^= 1;
      cur_harq->round = 0;
      cur_harq->state = INACTIVE; // passed -> make inactive. can be used by scheduder for next grant
      LOG_D(MAC,
            "Ulharq id %d crc passed for RNTI %04x\n",
            hrq_id,
            crc_pdu->rnti);
    } else {
      cur_harq->round++;
      cur_harq->state = ACTIVE_NOT_SCHED;
      LOG_D(MAC,
            "Ulharq id %d crc failed for RNTI %04x\n",
            hrq_id,
            crc_pdu->rnti);
    }

    if (!(cur_harq->round<max_harq_rounds)) {
      cur_harq->ndi ^= 1;
      cur_harq->state = INACTIVE; // failed after 4 rounds -> make inactive
      cur_harq->round = 0;
      LOG_D(MAC,
            "RNTI %04x: Ulharq id %d crc failed in all rounds\n",
            crc_pdu->rnti,
            hrq_id);
      UE_info->mac_stats[UE_id].ulsch_errors++;
    }
    return;
  } else
    LOG_W(MAC,
          "Incorrect ULSCH HARQ PID %d or invalid state %d for RNTI %04x "
          "(ignore this warning for RA)\n",
          hrq_id,
          cur_harq->state,
          crc_pdu->rnti);
}

/*
* When data are received on PHY and transmitted to MAC
*/
void nr_rx_sdu(const module_id_t gnb_mod_idP,
               const int CC_idP,
               const frame_t frameP,
               const sub_frame_t slotP,
               const rnti_t rntiP,
               uint8_t *sduP,
               const uint16_t sdu_lenP,
               const uint16_t timing_advance,
               const uint8_t ul_cqi,
               const uint16_t rssi){
  int current_rnti = 0, UE_id = -1, harq_pid = 0;
  gNB_MAC_INST *gNB_mac = NULL;
  NR_UE_info_t *UE_info = NULL;
  NR_UE_sched_ctrl_t *UE_scheduling_control = NULL;

  current_rnti = rntiP;
  UE_id = find_nr_UE_id(gnb_mod_idP, current_rnti);
  gNB_mac = RC.nrmac[gnb_mod_idP];
  UE_info = &gNB_mac->UE_info;
  int target_snrx10 = gNB_mac->pusch_target_snrx10;

  if (sduP != NULL) {
    T(T_GNB_MAC_UL_PDU_WITH_DATA, T_INT(gnb_mod_idP), T_INT(CC_idP),
      T_INT(rntiP), T_INT(frameP), T_INT(slotP), T_INT(-1) /* harq_pid */,
      T_BUFFER(sduP, sdu_lenP));
  }

  if (UE_id != -1) {
    UE_scheduling_control = &(UE_info->UE_sched_ctrl[UE_id]);

    UE_info->mac_stats[UE_id].ulsch_total_bytes_rx += sdu_lenP;
    LOG_D(MAC, "[gNB %d][PUSCH %d] CC_id %d %d.%d Received ULSCH sdu from PHY (rnti %x, UE_id %d) ul_cqi %d\n",
          gnb_mod_idP,
          harq_pid,
          CC_idP,
          frameP,
          slotP,
          current_rnti,
          UE_id,
          ul_cqi);

    // if not missed detection (10dB threshold for now)
    if (UE_scheduling_control->ul_rssi < (100+rssi)) {
      UE_scheduling_control->tpc0 = nr_get_tpc(target_snrx10,ul_cqi,30);
      if (timing_advance != 0xffff)
        UE_scheduling_control->ta_update = timing_advance;
      UE_scheduling_control->ul_rssi = rssi;
      LOG_D(MAC, "[UE %d] PUSCH TPC %d and TA %d\n",UE_id,UE_scheduling_control->tpc0,UE_scheduling_control->ta_update);
    }
    else{
      UE_scheduling_control->tpc0 = 1;
    }

#if defined(ENABLE_MAC_PAYLOAD_DEBUG)

    LOG_I(MAC, "Printing received UL MAC payload at gNB side: %d \n");
    for (int i = 0; i < sdu_lenP ; i++) {
	  //harq_process_ul_ue->a[i] = (unsigned char) rand();
	  //printf("a[%d]=0x%02x\n",i,harq_process_ul_ue->a[i]);
	  printf("%02x ",(unsigned char)sduP[i]);
    }
    printf("\n");

#endif

    if (sduP != NULL){
      LOG_D(MAC, "Received PDU at MAC gNB \n");

      UE_scheduling_control->sched_ul_bytes -= UE_info->mac_stats[UE_id].ulsch_current_bytes;
      if (UE_scheduling_control->sched_ul_bytes < 0)
        UE_scheduling_control->sched_ul_bytes = 0;

      nr_process_mac_pdu(gnb_mod_idP, current_rnti, CC_idP, frameP, sduP, sdu_lenP);
    }
    else {
      NR_UE_ul_harq_t *cur_harq = &UE_scheduling_control->ul_harq_processes[harq_pid];
      /* reduce sched_ul_bytes when cur_harq->round == 3 */
      if (cur_harq->round == 3){
        UE_scheduling_control->sched_ul_bytes -= UE_info->mac_stats[UE_id].ulsch_current_bytes;
        if (UE_scheduling_control->sched_ul_bytes < 0)
          UE_scheduling_control->sched_ul_bytes = 0;
      }
    }
  } else {
    if (!sduP) // check that CRC passed
      return;

    /* we don't know this UE (yet). Check whether there is a ongoing RA (Msg 3)
     * and check the corresponding UE's RNTI match, in which case we activate
     * it. */
    for (int i = 0; i < NR_NB_RA_PROC_MAX; ++i) {
      NR_RA_t *ra = &gNB_mac->common_channels[CC_idP].ra[i];
      if (ra->state != WAIT_Msg3)
        continue;

      // random access pusch with TC-RNTI
      if (ra->rnti != current_rnti) {
        LOG_W(MAC,
              "expected TC-RNTI %04x to match current RNTI %04x\n",
              ra->rnti,
              current_rnti);
        continue;
      }
      const int UE_id = add_new_nr_ue(gnb_mod_idP, ra->rnti);
      UE_info->secondaryCellGroup[UE_id] = ra->secondaryCellGroup;
      compute_csi_bitlen(ra->secondaryCellGroup, UE_info, UE_id);
      UE_info->UE_beam_index[UE_id] = ra->beam_id;
      struct NR_ServingCellConfig__downlinkBWP_ToAddModList *bwpList = ra->secondaryCellGroup->spCellConfig->spCellConfigDedicated->downlinkBWP_ToAddModList;
      AssertFatal(bwpList->list.count == 1,
                  "downlinkBWP_ToAddModList has %d BWP!\n",
                  bwpList->list.count);
      const int bwp_id = 1;
      UE_info->UE_sched_ctrl[UE_id].active_bwp = bwpList->list.array[bwp_id - 1];
      struct NR_UplinkConfig__uplinkBWP_ToAddModList *ubwpList = ra->secondaryCellGroup->spCellConfig->spCellConfigDedicated->uplinkConfig->uplinkBWP_ToAddModList;
      AssertFatal(ubwpList->list.count == 1,
                  "uplinkBWP_ToAddModList has %d BWP!\n",
                  ubwpList->list.count);
      UE_info->UE_sched_ctrl[UE_id].active_ubwp = ubwpList->list.array[bwp_id - 1];
      LOG_I(MAC,
            "[gNB %d][RAPROC] PUSCH with TC_RNTI %x received correctly, "
            "adding UE MAC Context UE_id %d/RNTI %04x\n",
            gnb_mod_idP,
            current_rnti,
            UE_id,
            ra->rnti);
      // re-initialize ta update variables afrer RA procedure completion
      UE_info->UE_sched_ctrl[UE_id].ta_frame = frameP;

      free(ra->preambles.preamble_list);
      ra->state = RA_IDLE;
      LOG_I(MAC,
            "reset RA state information for RA-RNTI %04x/index %d\n",
            ra->rnti,
            i);

      return;
    }
  }
}

long get_K2(NR_BWP_Uplink_t *ubwp, int time_domain_assignment, int mu) {
  DevAssert(ubwp);
  const NR_PUSCH_TimeDomainResourceAllocation_t *tda_list = ubwp->bwp_Common->pusch_ConfigCommon->choice.setup->pusch_TimeDomainAllocationList->list.array[time_domain_assignment];
  if (tda_list->k2)
    return *tda_list->k2;
  else if (mu < 2)
    return 1;
  else if (mu == 2)
    return 2;
  else
    return 3;
}

int8_t select_ul_harq_pid(NR_UE_sched_ctrl_t *sched_ctrl) {
  const uint8_t max_ul_harq_pids = 3; // temp: for testing
  // schedule active harq processes
  for (uint8_t hrq_id = 0; hrq_id < max_ul_harq_pids; hrq_id++) {
    NR_UE_ul_harq_t *cur_harq = &sched_ctrl->ul_harq_processes[hrq_id];
    if (cur_harq->state == ACTIVE_NOT_SCHED) {
      LOG_D(MAC, "Found ulharq id %d, scheduling it for retransmission\n", hrq_id);
      return hrq_id;
    }
  }

  // schedule new harq processes
  for (uint8_t hrq_id=0; hrq_id < max_ul_harq_pids; hrq_id++) {
    NR_UE_ul_harq_t *cur_harq = &sched_ctrl->ul_harq_processes[hrq_id];
    if (cur_harq->state == INACTIVE) {
      LOG_D(MAC, "Found new ulharq id %d, scheduling it\n", hrq_id);
      return hrq_id;
    }
  }
  LOG_E(MAC, "All UL HARQ processes are busy. Cannot schedule ULSCH\n");
  return -1;
}

bool nr_simple_ulsch_preprocessor(module_id_t module_id,
                                  frame_t frame,
                                  sub_frame_t slot,
                                  int num_slots_per_tdd,
                                  uint64_t ulsch_in_slot_bitmap) {
  gNB_MAC_INST *nr_mac = RC.nrmac[module_id];
  NR_COMMON_channels_t *cc = nr_mac->common_channels;
  NR_ServingCellConfigCommon_t *scc = cc->ServingCellConfigCommon;
  const int mu = scc->uplinkConfigCommon->initialUplinkBWP->genericParameters.subcarrierSpacing;
  NR_UE_info_t *UE_info = &nr_mac->UE_info;

  AssertFatal(UE_info->num_UEs <= 1,
              "%s() cannot handle more than one UE, but found %d\n",
              __func__,
              UE_info->num_UEs);
  if (UE_info->num_UEs == 0)
    return false;

  const int UE_id = 0;
  const int CC_id = 0;
  NR_UE_sched_ctrl_t *sched_ctrl = &UE_info->UE_sched_ctrl[UE_id];

  const int tda = 1;
  const struct NR_PUSCH_TimeDomainResourceAllocationList *tdaList =
    sched_ctrl->active_ubwp->bwp_Common->pusch_ConfigCommon->choice.setup->pusch_TimeDomainAllocationList;
  AssertFatal(tda < tdaList->list.count,
              "time domain assignment %d >= %d\n",
              tda,
              tdaList->list.count);
  int K2 = get_K2(sched_ctrl->active_ubwp, tda, mu);
  const int sched_frame = frame + (slot + K2 >= num_slots_per_tdd);
  const int sched_slot = (slot + K2) % num_slots_per_tdd;
  if (!is_xlsch_in_slot(ulsch_in_slot_bitmap, sched_slot))
    return false;

  sched_ctrl->sched_pusch.slot = sched_slot;
  sched_ctrl->sched_pusch.frame = sched_frame;

  const int target_ss = NR_SearchSpace__searchSpaceType_PR_ue_Specific;
  sched_ctrl->search_space = get_searchspace(sched_ctrl->active_bwp, target_ss);
  uint8_t nr_of_candidates;
  find_aggregation_candidates(&sched_ctrl->aggregation_level,
                              &nr_of_candidates,
                              sched_ctrl->search_space);
  sched_ctrl->coreset = get_coreset(
      sched_ctrl->active_bwp, sched_ctrl->search_space, 1 /* dedicated */);
  const int cid = sched_ctrl->coreset->controlResourceSetId;
  const uint16_t Y = UE_info->Y[UE_id][cid][slot];
  const int m = UE_info->num_pdcch_cand[UE_id][cid];
  sched_ctrl->cce_index = allocate_nr_CCEs(RC.nrmac[module_id],
                                           sched_ctrl->active_bwp,
                                           sched_ctrl->coreset,
                                           sched_ctrl->aggregation_level,
                                           Y,
                                           m,
                                           nr_of_candidates);
  if (sched_ctrl->cce_index < 0) {
    LOG_E(MAC, "%s(): CCE list not empty, couldn't schedule PUSCH\n", __func__);
    return false;
  }
  UE_info->num_pdcch_cand[UE_id][cid]++;

  sched_ctrl->sched_pusch.time_domain_allocation = tda;
  const long f = sched_ctrl->search_space->searchSpaceType->choice.ue_Specific->dci_Formats;
  const int dci_format = f ? NR_UL_DCI_FORMAT_0_1 : NR_UL_DCI_FORMAT_0_0;
  const uint8_t num_dmrs_cdm_grps_no_data = 1;
  /* we want to avoid a lengthy deduction of DMRS and other parameters in
   * every TTI if we can save it, so check whether dci_format, TDA, or
   * num_dmrs_cdm_grps_no_data has changed and only then recompute */
  NR_sched_pusch_save_t *ps = &sched_ctrl->pusch_save;
  if (ps->time_domain_allocation != tda
      || ps->dci_format != dci_format
      || ps->num_dmrs_cdm_grps_no_data != num_dmrs_cdm_grps_no_data)
    nr_save_pusch_fields(scc,
                         sched_ctrl->active_ubwp,
                         dci_format,
                         tda,
                         num_dmrs_cdm_grps_no_data,
                         ps);

  const int mcs = 9;
  NR_sched_pusch_t *sched_pusch = &sched_ctrl->sched_pusch;
  sched_pusch->mcs = mcs;

  /* Calculate TBS from MCS */
  sched_pusch->R = nr_get_code_rate_ul(mcs, ps->mcs_table);
  sched_pusch->Qm = nr_get_Qm_ul(mcs, ps->mcs_table);
  if (ps->pusch_Config->tp_pi2BPSK
      && ((ps->mcs_table == 3 && mcs < 2) || (ps->mcs_table == 4 && mcs < 6))) {
    sched_pusch->R >>= 1;
    sched_pusch->Qm <<= 1;
  }

  /* get first, largest unallocated region */
  uint16_t *vrb_map_UL =
      &RC.nrmac[module_id]->common_channels[CC_id].vrb_map_UL[sched_slot * 275];
  int rbStart = NRRIV2PRBOFFSET(sched_ctrl->active_bwp->bwp_Common->genericParameters.locationAndBandwidth, 275);
  const uint16_t bwpSize = NRRIV2BW(sched_ctrl->active_ubwp->bwp_Common->genericParameters.locationAndBandwidth,275);

  while (rbStart < bwpSize && vrb_map_UL[rbStart]) rbStart++;
  uint16_t rbSize = 4;

  sched_pusch->rbStart = rbStart;
  const int B = cmax(sched_ctrl->estimated_ul_buffer - sched_ctrl->sched_ul_bytes, 0);

  do {
    rbSize++;
    sched_pusch->rbSize = rbSize;
    sched_pusch->tb_size = nr_compute_tbs(sched_pusch->Qm,
                                          sched_pusch->R,
                                          sched_pusch->rbSize,
                                          ps->nrOfSymbols,
                                          ps->N_PRB_DMRS * ps->num_dmrs_symb,
                                          0, // nb_rb_oh
                                          0,
                                          1 /* NrOfLayers */)
                           >> 3;
  } while (rbStart + rbSize < bwpSize && !vrb_map_UL[rbStart+rbSize] &&
           sched_pusch->tb_size < B);
  LOG_D(MAC,"rbSize %d, TBS %d, est buf %d, sched_ul %d, B %d\n",
        rbSize, sched_pusch->tb_size, sched_ctrl->estimated_ul_buffer, sched_ctrl->sched_ul_bytes, B);

  /* mark the corresponding RBs as used */
  for (int rb = 0; rb < sched_ctrl->sched_pusch.rbSize; rb++)
    vrb_map_UL[rb + sched_ctrl->sched_pusch.rbStart] = 1;
  return true;
}

void nr_schedule_ulsch(module_id_t module_id,
                       frame_t frame,
                       sub_frame_t slot,
                       int num_slots_per_tdd,
                       int ul_slots,
                       uint64_t ulsch_in_slot_bitmap) {
  if (is_xlsch_in_slot(ulsch_in_slot_bitmap, slot % num_slots_per_tdd)) {
    LOG_D(MAC, "Current slot %d is NOT DL slot, cannot schedule DCI0 for UL data\n", slot);
    return;
  }
  bool do_sched = RC.nrmac[module_id]->pre_processor_ul(
      module_id, frame, slot, num_slots_per_tdd, ulsch_in_slot_bitmap);
  if (!do_sched)
    return;

  NR_ServingCellConfigCommon_t *scc = RC.nrmac[module_id]->common_channels[0].ServingCellConfigCommon;
  NR_UE_info_t *UE_info = &RC.nrmac[module_id]->UE_info;
  const NR_UE_list_t *UE_list = &UE_info->list;
  for (int UE_id = UE_list->head; UE_id >= 0; UE_id = UE_list->next[UE_id]) {
    NR_UE_sched_ctrl_t *sched_ctrl = &UE_info->UE_sched_ctrl[UE_id];
    /* dynamic PUSCH values (RB alloc, MCS, hence R, Qm, TBS) that change in
     * every TTI are pre-populated by the preprocessor and used below */
    NR_sched_pusch_t *sched_pusch = &sched_ctrl->sched_pusch;
    if (sched_pusch->rbSize <= 0)
      continue;

    uint16_t rnti = UE_info->rnti[UE_id];

    int8_t harq_id = select_ul_harq_pid(sched_ctrl);
    if (harq_id < 0) return;
    NR_UE_ul_harq_t *cur_harq = &sched_ctrl->ul_harq_processes[harq_id];
    cur_harq->state = ACTIVE_SCHED;
    cur_harq->last_tx_slot = sched_pusch->slot;

    int rnti_types[2] = { NR_RNTI_C, 0 };

    /* pre-computed PUSCH values that only change if time domain allocation,
     * DCI format, or DMRS parameters change. Updated in the preprocessor
     * through nr_save_pusch_fields() */
    NR_sched_pusch_save_t *ps = &sched_ctrl->pusch_save;

    /* Statistics */
    UE_info->mac_stats[UE_id].ulsch_rounds[cur_harq->round]++;
    if (cur_harq->round == 0) {
      UE_info->mac_stats[UE_id].ulsch_total_bytes_scheduled += sched_pusch->tb_size;
    } else {
      LOG_W(MAC,
            "%d.%2d UL retransmission RNTI %04x sched %d.%2d HARQ PID %d round %d NDI %d\n",
            frame,
            slot,
            rnti,
            sched_pusch->frame,
            sched_pusch->slot,
            harq_id,
            cur_harq->round,
            cur_harq->ndi);
    }

    LOG_D(MAC,
          "%4d.%2d RNTI %04x UL sched %4d.%2d start %d RBS %d MCS %d TBS %d HARQ PID %d round %d NDI %d\n",
          frame,
          slot,
          rnti,
          sched_pusch->frame,
          sched_pusch->slot,
          sched_pusch->rbStart,
          sched_pusch->rbSize,
          sched_pusch->mcs,
          sched_pusch->tb_size,
          harq_id,
          cur_harq->round,
          cur_harq->ndi);

    /* PUSCH in a later slot, but corresponding DCI now! */
    nfapi_nr_ul_tti_request_t *future_ul_tti_req = &RC.nrmac[module_id]->UL_tti_req_ahead[0][sched_pusch->slot];
    AssertFatal(future_ul_tti_req->SFN == sched_pusch->frame
                && future_ul_tti_req->Slot == sched_pusch->slot,
                "%d.%d future UL_tti_req's frame.slot %d.%d does not match PUSCH %d.%d\n",
                frame, slot,
                future_ul_tti_req->SFN,
                future_ul_tti_req->Slot,
                sched_pusch->frame,
                sched_pusch->slot);
    future_ul_tti_req->pdus_list[future_ul_tti_req->n_pdus].pdu_type = NFAPI_NR_UL_CONFIG_PUSCH_PDU_TYPE;
    future_ul_tti_req->pdus_list[future_ul_tti_req->n_pdus].pdu_size = sizeof(nfapi_nr_pusch_pdu_t);
    nfapi_nr_pusch_pdu_t *pusch_pdu = &future_ul_tti_req->pdus_list[future_ul_tti_req->n_pdus].pusch_pdu;
    memset(pusch_pdu, 0, sizeof(nfapi_nr_pusch_pdu_t));
    future_ul_tti_req->n_pdus += 1;

    LOG_D(MAC, "%4d.%2d Scheduling UE specific PUSCH\n", frame, slot);

    pusch_pdu->pdu_bit_map = PUSCH_PDU_BITMAP_PUSCH_DATA;
    pusch_pdu->rnti = rnti;
    pusch_pdu->handle = 0; //not yet used

    /* FAPI: BWP */
    pusch_pdu->bwp_size  = NRRIV2BW(sched_ctrl->active_ubwp->bwp_Common->genericParameters.locationAndBandwidth,275);
    pusch_pdu->bwp_start = NRRIV2PRBOFFSET(sched_ctrl->active_ubwp->bwp_Common->genericParameters.locationAndBandwidth,275);
    pusch_pdu->subcarrier_spacing = sched_ctrl->active_ubwp->bwp_Common->genericParameters.subcarrierSpacing;
    pusch_pdu->cyclic_prefix = 0;

    /* FAPI: PUSCH information always included */
    pusch_pdu->target_code_rate = sched_pusch->R;
    pusch_pdu->qam_mod_order = sched_pusch->Qm;
    pusch_pdu->mcs_index = sched_pusch->mcs;
    pusch_pdu->mcs_table = ps->mcs_table;
    pusch_pdu->transform_precoding = ps->transform_precoding;
    if (ps->pusch_Config->dataScramblingIdentityPUSCH)
      pusch_pdu->data_scrambling_id = *ps->pusch_Config->dataScramblingIdentityPUSCH;
    else
      pusch_pdu->data_scrambling_id = *scc->physCellId;
    pusch_pdu->nrOfLayers = 1;

    /* FAPI: DMRS */
    pusch_pdu->ul_dmrs_symb_pos = ps->ul_dmrs_symb_pos;
    pusch_pdu->dmrs_config_type = ps->dmrs_config_type;
    if (pusch_pdu->transform_precoding) { // transform precoding disabled
      long *scramblingid;
      if (pusch_pdu->scid == 0)
        scramblingid = ps->NR_DMRS_UplinkConfig->transformPrecodingDisabled->scramblingID0;
      else
        scramblingid = ps->NR_DMRS_UplinkConfig->transformPrecodingDisabled->scramblingID1;
      if (scramblingid == NULL)
        pusch_pdu->ul_dmrs_scrambling_id = *scc->physCellId;
      else
        pusch_pdu->ul_dmrs_scrambling_id = *scramblingid;
    }
    else {
      pusch_pdu->ul_dmrs_scrambling_id = *scc->physCellId;
      if (ps->NR_DMRS_UplinkConfig->transformPrecodingEnabled->nPUSCH_Identity != NULL)
        pusch_pdu->pusch_identity = *ps->NR_DMRS_UplinkConfig->transformPrecodingEnabled->nPUSCH_Identity;
      else
        pusch_pdu->pusch_identity = *scc->physCellId;
    }
    pusch_pdu->scid = 0;      // DMRS sequence initialization [TS38.211, sec 6.4.1.1.1]
    pusch_pdu->num_dmrs_cdm_grps_no_data = ps->num_dmrs_cdm_grps_no_data;
    pusch_pdu->dmrs_ports = 1;

    /* FAPI: Pusch Allocation in frequency domain */
    AssertFatal(ps->pusch_Config->resourceAllocation == NR_PUSCH_Config__resourceAllocation_resourceAllocationType1,
                "Only frequency resource allocation type 1 is currently supported\n");
    pusch_pdu->resource_alloc = 1; //type 1
    pusch_pdu->rb_start = sched_pusch->rbStart;
    pusch_pdu->rb_size = sched_pusch->rbSize;
    pusch_pdu->vrb_to_prb_mapping = 0;
    if (ps->pusch_Config->frequencyHopping==NULL)
      pusch_pdu->frequency_hopping = 0;
    else
      pusch_pdu->frequency_hopping = 1;

    /* FAPI: Resource Allocation in time domain */
    pusch_pdu->start_symbol_index = ps->startSymbolIndex;
    pusch_pdu->nr_of_symbols = ps->nrOfSymbols;

    /* PUSCH PDU */
    pusch_pdu->pusch_data.rv_index = nr_rv_round_map[cur_harq->round];
    pusch_pdu->pusch_data.harq_process_id = harq_id;
    pusch_pdu->pusch_data.new_data_indicator = cur_harq->ndi;
    pusch_pdu->pusch_data.tb_size = sched_pusch->tb_size;
    pusch_pdu->pusch_data.num_cb = 0; //CBG not supported

    UE_info->mac_stats[UE_id].ulsch_current_bytes = sched_pusch->tb_size;
    sched_ctrl->sched_ul_bytes += sched_pusch->tb_size;

    /* PUSCH PTRS */
    if (ps->NR_DMRS_UplinkConfig->phaseTrackingRS != NULL) {
      // TODO to be fixed from RRC config
      uint8_t ptrs_mcs1 = 2;  // higher layer parameter in PTRS-UplinkConfig
      uint8_t ptrs_mcs2 = 4;  // higher layer parameter in PTRS-UplinkConfig
      uint8_t ptrs_mcs3 = 10; // higher layer parameter in PTRS-UplinkConfig
      uint16_t n_rb0 = 25;    // higher layer parameter in PTRS-UplinkConfig
      uint16_t n_rb1 = 75;    // higher layer parameter in PTRS-UplinkConfig
      pusch_pdu->pusch_ptrs.ptrs_time_density = get_L_ptrs(ptrs_mcs1, ptrs_mcs2, ptrs_mcs3, pusch_pdu->mcs_index, pusch_pdu->mcs_table);
      pusch_pdu->pusch_ptrs.ptrs_freq_density = get_K_ptrs(n_rb0, n_rb1, pusch_pdu->rb_size);
      pusch_pdu->pusch_ptrs.ptrs_ports_list   = (nfapi_nr_ptrs_ports_t *) malloc(2*sizeof(nfapi_nr_ptrs_ports_t));
      pusch_pdu->pusch_ptrs.ptrs_ports_list[0].ptrs_re_offset = 0;

      pusch_pdu->pdu_bit_map |= PUSCH_PDU_BITMAP_PUSCH_PTRS; // enable PUSCH PTRS
    }
    else{
      pusch_pdu->pdu_bit_map &= ~PUSCH_PDU_BITMAP_PUSCH_PTRS; // disable PUSCH PTRS
    }

    nfapi_nr_ul_dci_request_t *ul_dci_req = &RC.nrmac[module_id]->UL_dci_req[0];
    ul_dci_req->SFN = frame;
    ul_dci_req->Slot = slot;
    nfapi_nr_ul_dci_request_pdus_t *ul_dci_request_pdu = &ul_dci_req->ul_dci_pdu_list[ul_dci_req->numPdus];
    memset(ul_dci_request_pdu, 0, sizeof(nfapi_nr_ul_dci_request_pdus_t));
    ul_dci_request_pdu->PDUType = NFAPI_NR_DL_TTI_PDCCH_PDU_TYPE;
    ul_dci_request_pdu->PDUSize = (uint8_t)(2+sizeof(nfapi_nr_dl_tti_pdcch_pdu));
    nfapi_nr_dl_tti_pdcch_pdu_rel15_t *pdcch_pdu_rel15 = &ul_dci_request_pdu->pdcch_pdu.pdcch_pdu_rel15;
    ul_dci_req->numPdus += 1;

    LOG_D(MAC,"Configuring ULDCI/PDCCH in %d.%d\n", frame,slot);

    nr_configure_pdcch(RC.nrmac[0],
                       pdcch_pdu_rel15,
                       rnti,
                       sched_ctrl->search_space,
                       sched_ctrl->coreset,
                       scc,
                       sched_ctrl->active_bwp,
                       sched_ctrl->aggregation_level,
                       sched_ctrl->cce_index);

    dci_pdu_rel15_t dci_pdu_rel15[MAX_DCI_CORESET];
    memset(dci_pdu_rel15, 0, sizeof(dci_pdu_rel15));
    NR_CellGroupConfig_t *secondaryCellGroup = UE_info->secondaryCellGroup[UE_id];
    const int n_ubwp = secondaryCellGroup->spCellConfig->spCellConfigDedicated->uplinkConfig->uplinkBWP_ToAddModList->list.count;
    // NOTE: below functions assume that dci_formats is an array corresponding
    // to all UL DCIs in the PDCCH, but for us it is a simple int. So before
    // having multiple UEs, the below need to be changed (IMO the functions
    // should fill for one DCI only and not handle all of them).
    config_uldci(sched_ctrl->active_ubwp,
                 pusch_pdu,
                 pdcch_pdu_rel15,
                 &dci_pdu_rel15[0],
                 &ps->dci_format,
                 ps->time_domain_allocation,
                 UE_info->UE_sched_ctrl[UE_id].tpc0,
                 n_ubwp,
                 sched_ctrl->active_bwp->bwp_Id);
    fill_dci_pdu_rel15(scc,
                       secondaryCellGroup,
                       pdcch_pdu_rel15,
                       dci_pdu_rel15,
                       &ps->dci_format,
                       rnti_types,
                       pusch_pdu->bwp_size,
                       sched_ctrl->active_bwp->bwp_Id);

    memset(sched_pusch, 0, sizeof(*sched_pusch));
  }
}
