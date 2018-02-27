/******************************************************************************
Copyright (C), 2014-2024, C-Data Tech. Co., Ltd.
  文 件 名   : ipc_public.h
  作    者   : jh.liang
  版 本 号   : 1.0.0
  生成日期   : 2015年3月13日
  功能描述   : 此文件定义IPC模块主程序
******************************************************************************/



#ifndef IPC_PUBLIC_H
#define IPC_PUBLIC_H

#define FALSE    0
#define TRUE      1
#ifndef BOOL
#define BOOL        unsigned char
#endif
#ifndef UCHAR
#define  UCHAR  unsigned char
#endif
#ifndef USHORT
#define  USHORT  unsigned short
#endif
#ifndef ULONG
#define  ULONG unsigned long
#endif

//定义整个系统最大支持的模块数
#define SYS_MAX_MODULE_NUM   255
#define MODULE_FOR_ONLY_SEND 0   /*给只发送事件的模块共用,实际上未进行注册,所以可多个模块共用.*/
#define MODULE_DATA_MIN    0
#define MODULE_SWSDK  1

#define MODULE_IPC    10
#define MODULE_LAYER2 11
#define MODULE_RSTP   12
#define MODULE_IGMPSN 13
#define MODULE_DHCPSN 14
#define MODULE_TRUNK  15
#define MODULE_QOS  16
#define MODULE_OLT    17
#define MODULE_GTF   18
#define MODULE_MONITOR 19
#define MODULE_SYSCTRL 20
#define MODULE_LACP     21
#define MODULE_VTYSH    22
#define MODULE_DOT1X    23
#define MODULE_ALARM_EVT 24

#define MODULE_DATA_MAX 30

/*以上为数据平面模块号*/
#define MODULE_SHELL_BASE 100
#define MODULE_SHELL(i) (i + MODULE_SHELL_BASE)
#define MODULE_SHELL_0 MODULE_SHELL(0)
#define MODULE_SHELL_1 MODULE_SHELL(1)
#define MODULE_SHELL_2 MODULE_SHELL(2)
#define MODULE_SHELL_3 MODULE_SHELL(3)
#define MODULE_SHELL_4 MODULE_SHELL(4)
#define MODULE_SHELL_5 MODULE_SHELL(5)

#define MODULE_END 255

//发布事件的消息头格式
typedef struct
{
    UCHAR    ucEventId;    //事件ID
    UCHAR    Res0;
    USHORT   usLen;
    ULONG    Res1;
}IPC_EVENT_RELEASE_HEAD;

//发布事件的消息格式
typedef struct
{
    IPC_EVENT_RELEASE_HEAD    EventMsgHead;
    char     data[0];
}IPC_EVENT_R_INFO;

#define APP_MSG_ACK_BIT 0x8000

//用户层消息包头(IPC消息类型是:命令IPC_MSG_CMD 或异步消息IPC_MSG_NOTIFY)
typedef struct
{
    short    MsgID;
    USHORT  DataLen;
    USHORT  SeqNo;
    short  RetCode;
    int    res0;
}IPC_APP_MSG_HEAD;

//用户层消息包格式(IPC消息类型是:操作消息IPC_MSG_CMD 或通知消息IPC_EVENT_RELEASE)
//整个结构体对应于接收回调函数中的pMsgOut，以及发送函数中的pAppMsgSend.
typedef struct
{
    IPC_APP_MSG_HEAD    MsgHead;
    char     data[0];
}IPC_APP_MSG;

#define IPC_EVENT_BASE  100
#define IPC_EVENT_MSG(i) (IPC_EVENT_BASE+i)

#define IPC_EVENT_PORT_ADD IPC_EVENT_MSG(1)/*事件数据:port;在汇聚中会用到*/
#define IPC_EVENT_PORT_DEL IPC_EVENT_MSG(2)
#define IPC_EVENT_PORT_UP_DOWN IPC_EVENT_MSG(3)/*事件数据:port*/
#define IPC_EVENT_PORT_DUPLEX  IPC_EVENT_MSG(4)
#define IPC_EVENT_PORT_ENABLE  IPC_EVENT_MSG(5)
#define IPC_EVENT_VLAN_ADD_PORT IPC_EVENT_MSG(6)/*事件数据:vlan+port*/
#define IPC_EVENT_VLAN_DEL_PORT IPC_EVENT_MSG(7)
#define IPC_EVENT_VLAN_ADD_DEL IPC_EVENT_MSG(8)/*事件数据:vlan号*/
#define IPC_EVENT_PORT_SPEED_CHANGE IPC_EVENT_MSG(9)
#define IPC_EVENT_8021Q_ENABLE IPC_EVENT_MSG(10)/*事件数据:0或1*/
#define IPC_EVENT_STG_STATE IPC_EVENT_MSG(11)/*事件数据:port+state*/
#define IPC_EVENT_SDK_INITED IPC_EVENT_MSG(12)
#define IPC_EVENT_TF_INITED IPC_EVENT_MSG(13)
#define IPC_EVENT_CLI_START  IPC_EVENT_MSG(14)
#define IPC_EVENT_TF_PORT_UP_DOWN IPC_EVENT_MSG(15) /* tf port plug/unplug */
#define IPC_EVENT_TF_PORT_LINK_UP_DOWN IPC_EVENT_MSG(16) /* tf port link up/down */
#define IPC_EVENT_ALARM_NOTIFY IPC_EVENT_MSG(17) /* alarm notify */
#define IPC_EVENT_GE_SFP_PORT_UP_DOWN IPC_EVENT_MSG(18) /* ge port plug/unplug */
#define IPC_EVENT_SW_L2_INITED  IPC_EVENT_MSG(19)
#define IPC_EVENT_SW_RSTP_INITED  IPC_EVENT_MSG(20)
#define IPC_EVENT_SW_TRUNK_INITED  IPC_EVENT_MSG(21)
#define IPC_EVENT_SW_IGMP_SN_INITED  IPC_EVENT_MSG(22)
#define IPC_EVENT_SW_DHCP_SN_INITED  IPC_EVENT_MSG(23)
#define IPC_EVENT_MODULE_KILLED  IPC_EVENT_MSG(24)
#define IPC_EVENT_CLI_INITED  IPC_EVENT_MSG(25)
#define IPC_EVENT_SYSCTRL_INITED  IPC_EVENT_MSG(26)
#define IPC_EVENT_CFG_INITED  IPC_EVENT_MSG(27)
#define IPC_EVENT_TERM_ALARM_MSG     IPC_EVENT_MSG(28)
#define IPC_EVENT_TERM_PRIO_NOTICE     IPC_EVENT_MSG(29)
#define IPC_EVENT_SYSLOG_PRIO_NOTICE     IPC_EVENT_MSG(30)
#define IPC_EVENT_TERM_DEBUG_NOTICE     IPC_EVENT_MSG(31)
#define IPC_EVENT_VLAN_IF_IP_SET IPC_EVENT_MSG(32)
#define IPC_EVENT_PHY_PORT_ADD IPC_EVENT_MSG(33)/*此事件只从sdk发出,只有trunk模块接收*/
#define IPC_EVENT_PHY_PORT_UP_DOWN IPC_EVENT_MSG(34)/*此事件只从sdk发出,只有trunk模块接收*/

#define IPC_EVENT_RSTP_PKT IPC_EVENT_MSG(35)
#define IPC_EVENT_IGMP_PKT IPC_EVENT_MSG(36)
#define IPC_EVENT_ARP_PKT  IPC_EVENT_MSG(37)
#define IPC_EVENT_DHCP_PKT  IPC_EVENT_MSG(38)

/*因为sdk初始化比较慢,其他的管理模块只有等待sdk初始化完成后才继续执行
所以sdk初始化完成之后发一个事件通知各模块*/
#define IPC_EVENT_KEY_NOTIFY  IPC_EVENT_MSG(39) /* 按键事件产生 */
#define IPC_EVENT_LACP_PKT  IPC_EVENT_MSG(40)
#define IPC_EVENT_SW_LACP_INITED  IPC_EVENT_MSG(41)
#define IPC_EVENT_SNMP_AGENT_SWITCH IPC_EVENT_MSG(42)
#define IPC_EVENT_OLT_WORK_MODE IPC_EVENT_MSG(43)
#define IPC_EVENT_IP_ROUTE_STATIC_SET IPC_EVENT_MSG(44)
#define IPC_EVENT_MC_CTRL_SET_OAM_SEND IPC_EVENT_MSG(45)
#define IPC_EVENT_RSTP_PORT_STATE IPC_EVENT_MSG(46)
#define IPC_EVENT_DOT1X_PKT             IPC_EVENT_MSG(47)
#define IPC_EVENT_SW_DOT1X_INITED       IPC_EVENT_MSG(48)
#define IPC_EVENT_SWITCH_LACP_TRUNK_MEM_DEL  IPC_EVENT_MSG(49)
#define IPC_EVENT_SWITCH_LACP_TRUNK_MEM_ADD  IPC_EVENT_MSG(50)
#define IPC_EVENT_VLAN_IF_SET IPC_EVENT_MSG(51)
#define IPC_EVENT_SNMP_TF_PORT_SWITCH  IPC_EVENT_MSG(52)
#define IPC_EVENT_IGMP_ENABLE_SET IPC_EVENT_MSG(53)
#define IPC_EVENT_DHCP_ENABLE_SET IPC_EVENT_MSG(54)
#define IPC_EVENT_DHCP_OPTION82_ENABLE_SET IPC_EVENT_MSG(55)
#define IPC_EVENT_PPPPLUS_PKT IPC_EVENT_MSG(56)

#define IPC_EVENT_MAX IPC_EVENT_MSG(80)

#define IPC_MSG_BASE  100

#define IPC_SYSTEM_MSG_BASE  (IPC_MSG_BASE+0) /*100*/
#define IPC_DEBUG_MSG(i) (IPC_SYSTEM_MSG_BASE+i)
#define IPC_DEBUG_MIN_MSG IPC_DEBUG_MSG(1)
#define IPC_DEBUG_PORT_MSG IPC_DEBUG_MSG(1)
#define IPC_DEBUG_MAC_MSG IPC_DEBUG_MSG(2)
#define IPC_DEBUG_STORM_MSG IPC_DEBUG_MSG(3)
#define IPC_DEBUG_VLAN_MSG IPC_DEBUG_MSG(4)
#define IPC_DEBUG_MIRROR_MSG IPC_DEBUG_MSG(5)
#define IPC_DEBUG_SDK_MSG IPC_DEBUG_MSG(6)
#define IPC_DEBUG_PKT_MSG IPC_DEBUG_MSG(7)
#define IPC_DEBUG_MAX_MSG IPC_DEBUG_MSG(9)/*109*/

#define IPC_PKT_MSG_BASE  (IPC_SYSTEM_MSG_BASE+10)
#define IPC_PKT_MSG(i) (IPC_PKT_MSG_BASE+i)
#define IPC_PKT_IGMP_MSG IPC_PKT_MSG(1)
#define IPC_PKT_RSTP_MSG IPC_PKT_MSG(2)
#define IPC_PKT_DHCP_MSG IPC_PKT_MSG(3)
#define IPC_PKT_ARP_MSG     IPC_PKT_MSG(4)
#define IPC_PKT_LACP_MSG    IPC_PKT_MSG(5)
#define IPC_PKT_DOT1X_MSG   IPC_PKT_MSG(6)
#define IPC_PKT_LOOP_DETECT_MSG  IPC_PKT_MSG(7)
#define IPC_TF_ONT_LOOP_DETECT_TIMES_MSG IPC_PKT_MSG(8)

#define IPC_PKT_PPPOEPLUS_MSG IPC_PKT_MSG(9)/*119*/

#define IPC_SYSCFG_MSG_BASE  (IPC_SYSTEM_MSG_BASE+20)
#define IPC_SYSCFG_MSG(i) (IPC_SYSCFG_MSG_BASE+i)
#define IPC_SYSCFG_HWADDR_MSG_SET IPC_SYSCFG_MSG(1)/*120*/
#define IPC_SYSCFG_IPADDR_MSG_SET IPC_SYSCFG_MSG(2)
#define IPC_SYSCFG_NETMASK_MSG_SET IPC_SYSCFG_MSG(3)
#define IPC_SYSCFG_GW_MSG_SET IPC_SYSCFG_MSG(4)
#define IPC_SYSCFG_MAX_MSG IPC_SYSCFG_MSG(9)/*129*/

#define IPC_SWITCH_MSG_BASE  (IPC_MSG_BASE+100)/*200*//*配置端口的物理属性*/
#define IPC_SWITCH_MSG(i) (IPC_SWITCH_MSG_BASE+i)
#define IPC_SWITCH_PORT_ENABLE_MSG_SET        IPC_SWITCH_MSG(1)
#define IPC_SWITCH_PORT_ENABLE_MSG_GET        IPC_SWITCH_MSG(2)
#define IPC_SWITCH_PORT_SPEED_MSG_SET         IPC_SWITCH_MSG(3)
#define IPC_SWITCH_PORT_SPEED_MSG_GET         IPC_SWITCH_MSG(4)
#define IPC_SWITCH_PORT_DUPLEX_MSG_SET        IPC_SWITCH_MSG(5)
#define IPC_SWITCH_PORT_DUPLEX_MSG_GET        IPC_SWITCH_MSG(6)
#define IPC_SWITCH_PORT_AUTONEG_MSG_SET       IPC_SWITCH_MSG(7)
#define IPC_SWITCH_PORT_AUTONEG_MSG_GET       IPC_SWITCH_MSG(8)
#define IPC_SWITCH_PORT_FLOWCTRL_MSG_SET      IPC_SWITCH_MSG(9)
#define IPC_SWITCH_PORT_FLOWCTRL_MSG_GET      IPC_SWITCH_MSG(10)
#define IPC_SWITCH_PORT_LEARN_MSG_SET         IPC_SWITCH_MSG(11)
#define IPC_SWITCH_PORT_LEARN_MSG_GET         IPC_SWITCH_MSG(12)
#define IPC_SWITCH_PORT_RATEEGR_MSG_SET       IPC_SWITCH_MSG(13)
#define IPC_SWITCH_PORT_RATEEGR_MSG_GET       IPC_SWITCH_MSG(14)
#define IPC_SWITCH_PORT_RATEING_MSG_SET       IPC_SWITCH_MSG(15)
#define IPC_SWITCH_PORT_RATEING_MSG_GET       IPC_SWITCH_MSG(16)
#define IPC_SWITCH_PORT_LINKSTATUS_MSG_GET    IPC_SWITCH_MSG(17)
#define IPC_SWITCH_PORT_VLAN_MSG_GET    IPC_SWITCH_MSG(18)
#define IPC_SWITCH_PORT_MTU_MSG_SET    IPC_SWITCH_MSG(19)
#define IPC_SWITCH_PORT_MTU_MSG_GET    IPC_SWITCH_MSG(20)

#define IPC_SWITCH_MAC_AGING_MSG_SET  IPC_SWITCH_MSG(21)
#define IPC_SWITCH_MAC_AGING_MSG_GET  IPC_SWITCH_MSG(22)
#define IPC_SWITCH_MAC_LIMIT_MSG_SET  IPC_SWITCH_MSG(23)
#define IPC_SWITCH_MAC_LIMIT_MSG_GET  IPC_SWITCH_MSG(24)
#define IPC_SWITCH_MAC_TABLE_MSG_GET  IPC_SWITCH_MSG(25)
#define IPC_SWITCH_UCMAC_MSG_ADD IPC_SWITCH_MSG(26)
#define IPC_SWITCH_UCMAC_MSG_DEL IPC_SWITCH_MSG(27)
#define IPC_SWITCH_MCMAC_MSG_ADD   IPC_SWITCH_MSG(28)
#define IPC_SWITCH_MCMAC_MSG_DEL   IPC_SWITCH_MSG(29)

#define IPC_SWITCH_MAC_MSG_DEL_ALL   IPC_SWITCH_MSG(30)
#define IPC_SWITCH_MAC_MSG_DEL_DYNAMIC  IPC_SWITCH_MSG(31)
#define IPC_SWITCH_MAC_MSG_DEL_STATIC  IPC_SWITCH_MSG(32)
#define IPC_SWITCH_MAC_MSG_DEL_BLACKHOLE  IPC_SWITCH_MSG(33)
#define IPC_SWITCH_MAC_MSG_DEL_BY_PORT   IPC_SWITCH_MSG(34)
#define IPC_SWITCH_MAC_MSG_DEL_BY_VLAN   IPC_SWITCH_MSG(35)
#define IPC_SWITCH_BLACKHOLE_MAC_MSG_ADD IPC_SWITCH_MSG(36)
#define IPC_SWITCH_BLACKHOLE_MAC_MSG_DEL IPC_SWITCH_MSG(37)
#define IPC_SWITCH_DYNAMIC_MAC_MSG_DEL IPC_SWITCH_MSG(38)

#define IPC_SWITCH_BCSTORM_RATE_MSG_SET   IPC_SWITCH_MSG(40)
#define IPC_SWITCH_BCSTORM_RATE_MSG_GET   IPC_SWITCH_MSG(41)
#define IPC_SWITCH_MCSTORM_RATE_MSG_SET   IPC_SWITCH_MSG(42)
#define IPC_SWITCH_MCSTORM_RATE_MSG_GET   IPC_SWITCH_MSG(43)
#define IPC_SWITCH_DLFSTORM_RATE_MSG_SET   IPC_SWITCH_MSG(44)
#define IPC_SWITCH_DLFSTORM_RATE_MSG_GET   IPC_SWITCH_MSG(45)
#define IPC_SWITCH_STORM_BW_MSG_SET   IPC_SWITCH_MSG(46)
#define IPC_SWITCH_STORM_BW_MSG_GET   IPC_SWITCH_MSG(47)

#define IPC_SWITCH_PVID_MSG_SET IPC_SWITCH_MSG(50)
#define IPC_SWITCH_PVID_MSG_GET IPC_SWITCH_MSG(51)
#define IPC_SWITCH_PPRI_MSG_SET IPC_SWITCH_MSG(52) /*port priority*/
#define IPC_SWITCH_PPRI_MSG_GET IPC_SWITCH_MSG(53)
#define IPC_SWITCH_VLAN_CREATE_MSG_SET  IPC_SWITCH_MSG(54)
#define IPC_SWITCH_VLAN_DESTROY_MSG_SET IPC_SWITCH_MSG(55)
#define IPC_SWITCH_VLAN_DESTROYALL_MSG_SET IPC_SWITCH_MSG(56)
#define IPC_SWITCH_VLAN_ADDPORT_MSG_SET IPC_SWITCH_MSG(57)
#define IPC_SWITCH_VLAN_DELPORT_MSG_SET IPC_SWITCH_MSG(58)
#define IPC_SWITCH_VLAN_PORT_DISCARD_MSG_SET IPC_SWITCH_MSG(59) /*used for bcm_port_discard_set*/
#define IPC_SWITCH_VLAN_DEFAULT_MSG_SET IPC_SWITCH_MSG(60)
#define IPC_SWITCH_VLAN_INFILTER_MSG_SET IPC_SWITCH_MSG(61) /*INGRESS FILTER*/
#define IPC_SWITCH_VLAN_PORT_MSG_GET IPC_SWITCH_MSG(62)
#define IPC_SWITCH_VLAN_PORT_MODE_MSG_SET IPC_SWITCH_MSG(63) /*配置端口的vlan模式*/
#define IPC_SWITCH_VLAN_PORT_MODE_MSG_GET IPC_SWITCH_MSG(64)
#define IPC_SWITCH_VLAN_PORT_ACCESS_MSG_SET IPC_SWITCH_MSG(65) /*配置端口的vlan模式*/
#define IPC_SWITCH_VLAN_PORT_ACCESS_MSG_GET IPC_SWITCH_MSG(66)
#define IPC_SWITCH_VLAN_PORT_TRUNK_MSG_SET IPC_SWITCH_MSG(67) /*配置端口的vlan模式*/
#define IPC_SWITCH_VLAN_PORT_TRUNK_MSG_GET IPC_SWITCH_MSG(68)
#define IPC_SWITCH_VLAN_PORT_HYBRID_MSG_SET IPC_SWITCH_MSG(69) /*配置端口的vlan模式*/
#define IPC_SWITCH_VLAN_PORT_HYBRID_MSG_GET IPC_SWITCH_MSG(70)
#define IPC_SWITCH_VLAN_EXIST_MSG_GET IPC_SWITCH_MSG(71)
#define IPC_SWITCH_VLAN_ACTION_MSG_SET IPC_SWITCH_MSG(72) /*vlan action*/


#define IPC_SWITCH_MIRROR_INIT_MSG_SET IPC_SWITCH_MSG(80)
#define IPC_SWITCH_MIRROR_MODE_MSG_SET IPC_SWITCH_MSG(82)
#define IPC_SWITCH_MIRROR_DEST_MSG_SET IPC_SWITCH_MSG(83)
#define IPC_SWITCH_MIRROR_DEST_MSG_UNSET IPC_SWITCH_MSG(84)
#define IPC_SWITCH_MIRROR_DEST_MSG_GET IPC_SWITCH_MSG(85)
#define IPC_SWITCH_MIRROR_SRC_MSG_SET IPC_SWITCH_MSG(86)
#define IPC_SWITCH_MIRROR_SRC_MSG_ADD IPC_SWITCH_MSG(87)
#define IPC_SWITCH_MIRROR_SRC_MSG_DEL IPC_SWITCH_MSG(88)

#define IPC_SWITCH_PORT_STATS_RATE_MSG_GET IPC_SWITCH_MSG(91) 
#define IPC_SWITCH_PORT_COUNTER_MSG_GET IPC_SWITCH_MSG(92)
#define IPC_SWITCH_PORT_COUNTER_CLEAR_MSG_SET IPC_SWITCH_MSG(93) 
#define IPC_SWITCH_PORT_STATISTICS_15MIN_MSG_GET IPC_SWITCH_MSG(94)
#define IPC_SWITCH_PORT_STATISTICS_15MIN_MSG_SET IPC_SWITCH_MSG(95)
#define IPC_SWITCH_PORT_STATISTICS_24HOUR_MSG_GET IPC_SWITCH_MSG(96)
#define IPC_SWITCH_PORT_STATISTICS_24HOUR_MSG_SET IPC_SWITCH_MSG(97)
//#define IPC_ETF_ONU_ONLINE_FLAG_AND_PORT_NUM_GET IPC_SWITCH_MSG(98)

#define IPC_SWITCH_PKT_CONTROL_MSG_SET IPC_SWITCH_MSG(101) /*设置switch的数据包上cpu*/
#define IPC_SWITCH_PKT_FILTER_MSG_SET IPC_SWITCH_MSG(102)  /*设置驱动的数据包上用户层还是上内核协议栈*/
#define IPC_SWITCH_PKT_BPDU_MSG_SET IPC_SWITCH_MSG(103)  
#define IPC_SWITCH_PKT_BPDU_MSG_DEL IPC_SWITCH_MSG(104)  
#define IPC_SWITCH_CREATE_INTERFACE_MSG_SET IPC_SWITCH_MSG(105)  
#define IPC_SWITCH_PORT_SFP_MSG_GET        IPC_SWITCH_MSG(106)
#define IPC_SWITCH_PORT_DTAG_MODE_MSG        IPC_SWITCH_MSG(107)  /* set vlan dtag mode */
#define IPC_SWITCH_PORT_DTAG_RANGE_MSG        IPC_SWITCH_MSG(108)
#define IPC_SWITCH_PORT_DSCP_TO_PRIO_MSG      IPC_SWITCH_MSG(109)
#define IPC_SWITCH_PORT_ENCAP_MSG_SET          IPC_SWITCH_MSG(110)
#define IPC_SWITCH_PORT_NAME_MSG_GET IPC_SWITCH_MSG(111)
#define IPC_SWITCH_PORT_NAME_MSG_SET IPC_SWITCH_MSG(112)


#define IPC_IGMP_MSG_BASE  (IPC_MSG_BASE+250)/*350*/
#define IPC_IGMP_MSG(i) (IPC_IGMP_MSG_BASE+i)

#define IPC_SWITCH_MCAST_MODE_MSG_SET IPC_IGMP_MSG(1)
#define IPC_SWITCH_MCAST_PBMP_MSG_GET IPC_IGMP_MSG(2)

#define IPC_SWITCH_IGMP_JION_MSG_SET IPC_IGMP_MSG(3)
#define IPC_SWITCH_IGMP_LEAVE_MSG_SET IPC_IGMP_MSG(4)
#define IPC_SWITCH_IGMP_MCADDRRM_MSG_SET IPC_IGMP_MSG(5)  //mcaddr remove
#define IPC_SWITCH_IGMP_ENTRYADD_MSG_SET IPC_IGMP_MSG(6)  //mcaddr remove
#define IPC_SWITCH_IGMP_ENTRYDEL_MSG_SET IPC_IGMP_MSG(7)  //mcaddr remove
#define IPC_SWITCH_IGMP_ENTRY_MSG_GET IPC_IGMP_MSG(8)  //mcaddr remove
#define IPC_SWITCH_IGMP_CFG_GET IPC_IGMP_MSG(9)
#define IPC_SWITCH_IGMP_ENABLE_SET IPC_IGMP_MSG(10)
#define IPC_SWITCH_IGMP_DROP_UNKNOWN_SET IPC_IGMP_MSG(11)
#define IPC_SWITCH_IGMP_FAST_LEAVE_SET IPC_IGMP_MSG(12)
#define IPC_SWITCH_IGMP_HOST_AGING_TIME_SET IPC_IGMP_MSG(13)
#define IPC_SWITCH_IGMP_ROUTER_AGING_TIME_SET IPC_IGMP_MSG(14)
#define IPC_SWITCH_IGMP_QUERY_SET IPC_IGMP_MSG(15)
#define IPC_SWITCH_IGMP_QUERY_INTERVAL_SET IPC_IGMP_MSG(16)
#define IPC_SWITCH_IGMP_QUERY_MAX_RESTFSE_TIME_SET IPC_IGMP_MSG(17)
#define IPC_SWITCH_IGMP_QUERY_SOURCE_IP_SET IPC_IGMP_MSG(18)

#define IPC_SWITCH_IGMP_8021Q_MSG_GET IPC_IGMP_MSG(19)
#define IPC_SWITCH_IGMP_VLANCHECK_MSG_GET IPC_IGMP_MSG(20)
#define IPC_SWITCH_IGMP_TABLE_ALL_MSG_GET IPC_IGMP_MSG(21)
#define IPC_SWITCH_IGMP_TABLE_GE_MSG_GET IPC_IGMP_MSG(22)
#define IPC_SWITCH_IGMP_TABLE_TF_MSG_GET IPC_IGMP_MSG(23)
#define IPC_SWITCH_IGMP_TABLE_XGE_MSG_GET IPC_IGMP_MSG(24)
#define IPC_SWITCH_IGMP_TABLE_IP_MSG_GET IPC_IGMP_MSG(25)
#define IPC_SWITCH_IGMP_TABLE_STATIC_MSG_GET IPC_IGMP_MSG(26)
#define IPC_SWITCH_IGMP_TABLE_VLAN_MSG_GET IPC_IGMP_MSG(27)

#define IPC_SWITCH_PFM_MODE_MSG_SET IPC_IGMP_MSG(28)
#define IPC_SWITCH_MULTILVLAN_ADD_MSG_SET IPC_IGMP_MSG(29)
#define IPC_SWITCH_MULTILVLAN_DEL_MSG_SET IPC_IGMP_MSG(30)
#define IPC_SWITCH_MULTILVLAN_ADDPORT_MSG_SET IPC_IGMP_MSG(31)
#define IPC_SWITCH_MULTILVLAN_DELPORT_MSG_SET IPC_IGMP_MSG(32)
#define IPC_SWITCH_MULTILVLAN_INFO_MSG_GET IPC_IGMP_MSG(33)
#define IPC_SWITCH_MULTILVLAN_MSG_GET IPC_IGMP_MSG(34)
#define IPC_SWITCH_MCAST_MODE_MSG_GET IPC_IGMP_MSG(35)
#define IPC_PKT_IGMP_FROM_8022_MSG IPC_IGMP_MSG(36)
#define IPC_SWITCH_ONU_MCAST_MODE_MSG_GET IPC_IGMP_MSG(37)
//#define IPC_DEBUG_IGMP_PKT_FROM_CS8022 IPC_IGMP_MSG(37)
#define IPC_SWITCH_IGMP_ONU_ID_GET IPC_IGMP_MSG(38)

#define IPC_RSTP_MSG_BASE  (IPC_MSG_BASE+300)/*400*/
#define IPC_RSTP_MSG(i) (IPC_RSTP_MSG_BASE+i)

#define IPC_SWITCH_RSTP_PORTSTATE_MSG_SET IPC_RSTP_MSG(1)//端口的stg状态
#define IPC_SWITCH_RSTP_PORTSTATE_MSG_GET IPC_RSTP_MSG(2)//端口的stg状态

#define IPC_SWITCH_RSTP_PORTTRUNKID_MSG_GET IPC_RSTP_MSG(3)//rstp从trunk取回来端口的trunk
#define IPC_SWITCH_RSTP_PORTISTRUNK_MSG_GET IPC_RSTP_MSG(4)//判断某个端口是否是某个trunk的成员
#define IPC_SWITCH_RSTP_PORTMASTER_MSG_GET IPC_RSTP_MSG(5)//rstp从trunk中取得该汇聚组的主端口
#define IPC_SWITCH_RSTP_ENABLE_MSG_SET IPC_RSTP_MSG(6)
#define IPC_SWITCH_RSTP_BRG_FDELAY_SET IPC_RSTP_MSG(7)
#define IPC_SWITCH_RSTP_BRG_PRI_SET IPC_RSTP_MSG(8)
#define IPC_SWITCH_RSTP_BRG_GET IPC_RSTP_MSG(9)
#define IPC_SWITCH_RSTP_CFG_GET IPC_RSTP_MSG(10)
#define IPC_SWITCH_RSTP_PORT_GET IPC_RSTP_MSG(11)
#define IPC_SWITCH_RSTP_MAX_AGE_SET IPC_RSTP_MSG(12)
#define IPC_SWITCH_RSTP_PORT_PRIO_SET IPC_RSTP_MSG(13)
#define IPC_SWITCH_RSTP_PORT_PATH_COST_SET IPC_RSTP_MSG(14)
#define IPC_SWITCH_RSTP_PORT_P2P_SET IPC_RSTP_MSG(15)
#define IPC_SWITCH_RSTP_PORT_EDGE_SET IPC_RSTP_MSG(16)
#define IPC_SWITCH_RSTP_PORT_MCHECK_SET IPC_RSTP_MSG(17)
#define IPC_SWITCH_RSTP_BRG_HELLO_TIME_SET IPC_RSTP_MSG(18)
#define IPC_SWITCH_RSTP_PORT_CFG_GET IPC_RSTP_MSG(19)


#define IPC_DHCP_MSG_BASE  (IPC_MSG_BASE+350)/*450*/
#define IPC_DHCP_MSG(i) (IPC_DHCP_MSG_BASE+i)

#define IPC_SWITCH_DHCP_ENABLE_SET IPC_DHCP_MSG(1)
#define IPC_SWITCH_DHCP_VLAN_MSG_ADD IPC_DHCP_MSG(2)
#define IPC_SWITCH_DHCP_VLAN_MSG_DEL IPC_DHCP_MSG(3)
#define IPC_SWITCH_DHCP_TRUST_PORT_SET IPC_DHCP_MSG(4)
#define IPC_SWITCH_DHCP_UNTRUST_PORT_SET IPC_DHCP_MSG(5)
#define IPC_SWITCH_DHCP_CHADDR_CHECK_SET IPC_DHCP_MSG(6)
#define IPC_SWITCH_DHCP_PORT_LIMIT_RATE_SET IPC_DHCP_MSG(7)
#define IPC_SWITCH_DHCP_PORT_RECOVERY_ADMIN_SET IPC_DHCP_MSG(8)
#define IPC_SWITCH_DHCP_PORT_RECOVERY_INTERVAL_SET IPC_DHCP_MSG(9)
#define IPC_SWITCH_DHCP_OPTION82_ADMIN_SET IPC_DHCP_MSG(10)
#define IPC_SWITCH_DHCP_OPTION82_POLICY_SET IPC_DHCP_MSG(11)
#define IPC_SWITCH_DHCP_CIRCUITID_STRING_SET IPC_DHCP_MSG(12)
#define IPC_SWITCH_DHCP_CIRCUITID_FORMAT_SET IPC_DHCP_MSG(13)
#define IPC_SWITCH_DHCP_REMOTEID_STRING_SET IPC_DHCP_MSG(14)
#define IPC_SWITCH_DHCP_REMOTEID_FORMAT_SET IPC_DHCP_MSG(15)
#define IPC_SWITCH_DHCP_BINDING IPC_DHCP_MSG(16)
#define IPC_SWITCH_DHCP_BIND_TABLE_CLEAR_ALL IPC_DHCP_MSG(17)
#define IPC_SWITCH_DHCP_BIND_TABLE_CLEAR_BY_TYPE IPC_DHCP_MSG(18)
#define IPC_SWITCH_DHCP_BIND_TABLE_CLEAR_BY_VID IPC_DHCP_MSG(19)
#define IPC_SWITCH_DHCP_BIND_TABLE_CLEAR_BY_MAC_VID IPC_DHCP_MSG(20)
#define IPC_SWITCH_DHCP_ENTRY_DEL_TIME_SET IPC_DHCP_MSG(21)
#define IPC_SWITCH_DHCP_DELAY_SET IPC_DHCP_MSG(22)
#define IPC_SWITCH_DHCP_CONFIGURE_GET IPC_DHCP_MSG(23)
#define IPC_SWITCH_DHCP_BIND_TABLE_GET IPC_DHCP_MSG(24)
#define IPC_SWITCH_DHCP_BIND_TABLE_COUNT_GET IPC_DHCP_MSG(25)
#define IPC_SWITCH_DHCP_BIND_TABLE_WRITE_TO_FLASH IPC_DHCP_MSG(26)
#define IPC_SWITCH_DHCP_BIND_TABLE_SAVE_TO_TFTP IPC_DHCP_MSG(27)
#define IPC_SWITCH_ARP_DETECTION_SET IPC_DHCP_MSG(28)
#define IPC_SWITCH_ARP_REPLY_FAST_SET IPC_DHCP_MSG(29)



#define IPC_TRUNK_MSG_BASE  (IPC_MSG_BASE+400)/*500*/
#define IPC_TRUNK_MSG(i) (IPC_TRUNK_MSG_BASE+i)

#define IPC_SWITCH_TRUNK_CREATE_MSG_SET IPC_TRUNK_MSG(1)
#define IPC_SWITCH_TRUNK_DESTROY_MSG_SET IPC_TRUNK_MSG(2)
#define IPC_SWITCH_TRUNK_PORT_MSG_SET IPC_TRUNK_MSG(3)
#define IPC_SWITCH_TRUNK_INFO_MSG_GET IPC_TRUNK_MSG(4)
#define IPC_SWITCH_TRUNK_LOADBALANCE_MSG_SET IPC_TRUNK_MSG(5) /*配置汇聚组的负载均衡的方式*/
#define IPC_SWITCH_TRUNK_MEMBER_ADD_MSG_SET IPC_TRUNK_MSG(6)
#define IPC_SWITCH_TRUNK_MEMBER_DEL_MSG_SET IPC_TRUNK_MSG(7)
#define IPC_SWITCH_TRUNK_SHOW_MSG_SET IPC_TRUNK_MSG(8)
#define IPC_SWITCH_TRUNK_MASTER_MSG_GET IPC_TRUNK_MSG(9)

#define IPC_POLICY_VLAN_MSG_BASE  (IPC_MSG_BASE+450)/*550*/
#define IPC_POLICY_VLAN_MSG(i) (IPC_POLICY_VLAN_MSG_BASE+i)

#define IPC_SWITCH_IPSUBNET_VLAN_MSG_ADD IPC_POLICY_VLAN_MSG(1) 
#define IPC_SWITCH_IPSUBNET_VLAN_MSG_DEL  IPC_POLICY_VLAN_MSG(2) 
#define IPC_SWITCH_IPSUBNET_VLAN_MSG_CLEAR  IPC_POLICY_VLAN_MSG(3) 
#define IPC_SWITCH_PROTOCOL_VLAN_MSG_ADD  IPC_POLICY_VLAN_MSG(4) 
#define IPC_SWITCH_PROTOCOL_VLAN_MSG_DEL  IPC_POLICY_VLAN_MSG(5) 
#define IPC_SWITCH_PROTOCOL_VLAN_MSG_CLEAR  IPC_POLICY_VLAN_MSG(6) 

#define IPC_SWITCH_MAC_VLAN_MSG_ADD  IPC_POLICY_VLAN_MSG(7) 
#define IPC_SWITCH_MAC_VLAN_MSG_DEL  IPC_POLICY_VLAN_MSG(8) 
#define IPC_SWITCH_MAC_VLAN_MSG_CLEAR  IPC_POLICY_VLAN_MSG(9) 

#define IPC_SWITCH_IN_XVLAN_MSG_ADD  IPC_POLICY_VLAN_MSG(10) 
#define IPC_SWITCH_IN_XVLAN_MSG_DEL  IPC_POLICY_VLAN_MSG(11) 
#define IPC_SWITCH_IN_XVLAN_MSG_CLEAR  IPC_POLICY_VLAN_MSG(12) 
#define IPC_SWITCH_SWITCH_XVLAN_MSG  IPC_POLICY_VLAN_MSG(13) 
#define IPC_SWITCH_SWITCH_XVLAN_PORT_CTRL_MSG  IPC_POLICY_VLAN_MSG(14) 

#define IPC_OLT_MSG_BASE  (IPC_MSG_BASE+500)/*600*/
#define IPC_OLT_MSG(i) (IPC_OLT_MSG_BASE+i)

#define IPC_OLT_MODE_SET IPC_OLT_MSG(1)
#define IPC_OLT_MODE_GET IPC_OLT_MSG(2)

typedef enum
{
    IPC_GTF_MSG_BASE = IPC_MSG_BASE+600, /*700*/

    IPC_GTF_MSG_END,
}IPC_GTF_MSG_DTE;

#define IPC_SYS_CTRL_MSG_BASE  (IPC_MSG_BASE+800)/*900*/
#define IPC_SYS_CTRL_MSG(i) (IPC_SYS_CTRL_MSG_BASE+i)
#define IPC_LOAD_FILE_TRANSMIT_SET          IPC_SYS_CTRL_MSG(1)
#define IPC_LOAD_FILE_TRANSMIT_GET          IPC_SYS_CTRL_MSG(2)
#define IPC_LOAD_FILE_TRANSMIT_STATUS       IPC_SYS_CTRL_MSG(3)
#define IPC_LOAD_MPU_OPERATION_SET          IPC_SYS_CTRL_MSG(4)
#define IPC_LOAD_MPU_OPERATION_GET          IPC_SYS_CTRL_MSG(5)
#define IPC_LOAD_MPU_OPERATION_STATUS       IPC_SYS_CTRL_MSG(6)
#define IPC_SYS_MGMT_IP_SET                 IPC_SYS_CTRL_MSG(7)
#define IPC_SYS_MGMTIF_INFO_GET             IPC_SYS_CTRL_MSG(8)
#define IPC_SYS_VLANIF_IP_SET               IPC_SYS_CTRL_MSG(9)
#define IPC_SYS_VLANIF_IP_GET               IPC_SYS_CTRL_MSG(10)
#define IPC_SYS_VLANIF_INFO_GET             IPC_SYS_CTRL_MSG(11)
#define IPC_SYS_CFG_FACTORY                 IPC_SYS_CTRL_MSG(12)
#define IPC_SYS_IP_ROUTE_STATIC_SET         IPC_SYS_CTRL_MSG(13)
#define IPC_SYS_IP_ROUTE_STATIC_GET         IPC_SYS_CTRL_MSG(14)
#define IPC_SYS_LOG_HOST_ADD                IPC_SYS_CTRL_MSG(15)
#define IPC_SYS_LOG_HOST_ACTIVATE           IPC_SYS_CTRL_MSG(16)
#define IPC_SYS_LOG_HOST_DEACTIVATE         IPC_SYS_CTRL_MSG(17)
#define IPC_SYS_LOG_HOST_DEL                IPC_SYS_CTRL_MSG(18)
#define IPC_SYS_LOG_HOST_LIST_GET           IPC_SYS_CTRL_MSG(19)
#define IPC_SYS_LOG_PARAM_SET               IPC_SYS_CTRL_MSG(20)
#define IPC_SYS_ENV_MAC_SET                 IPC_SYS_CTRL_MSG(21)
#define IPC_SYS_ENV_MODEL_SET               IPC_SYS_CTRL_MSG(22)
#define IPC_SYS_ENV_SN_SET                  IPC_SYS_CTRL_MSG(23)
#define IPC_SYS_ENV_VENDOR_SET              IPC_SYS_CTRL_MSG(24)
#define IPC_SYS_ENV_PARAM_GET               IPC_SYS_CTRL_MSG(25)
#define IPC_SYS_MONITOR_TF_PORT_INFO_GET   IPC_SYS_CTRL_MSG(26)
#define IPC_SYS_MONITOR_GE_PORT_INFO_GET    IPC_SYS_CTRL_MSG(27)
#define IPC_SYS_MONITOR_XGE_PORT_INFO_GET   IPC_SYS_CTRL_MSG(28)
#define IPC_SYS_MONITOR_SYS_HW_INFO_GET     IPC_SYS_CTRL_MSG(29)
#define IPC_SYS_MONITOR_XGE_CARD_INFO_GET   IPC_SYS_CTRL_MSG(30)
#define IPC_SYS_ALARM_TRIGGER   IPC_SYS_CTRL_MSG(31)
#define IPC_SYS_EVENT_TRIGGER IPC_SYS_CTRL_MSG(32)
#define IPC_LOAD_FILE_FTP_PARAMETER_SEND    IPC_SYS_CTRL_MSG(33)
#define IPC_SYS_MONITOR_TF_PORT_DISABLE    IPC_SYS_CTRL_MSG(34)
#define IPC_SYS_REBOOT_CTRL                 IPC_SYS_CTRL_MSG(35)
#define IPC_SYS_OPER_STATUS_GET             IPC_SYS_CTRL_MSG(36)

#define IPC_MONITOR_MSG_BASE  (IPC_MSG_BASE+900)/*1000*/
#define IPC_MONITOR_MSG(i) (IPC_MONITOR_MSG_BASE+i)

#define IPC_MONITOR_TF_PORT_INFO_GET  IPC_MONITOR_MSG(1)
#define IPC_MONITOR_GE_PORT_INFO_GET   IPC_MONITOR_MSG(2)
#define IPC_MONITOR_XGE_PORT_INFO_GET  IPC_MONITOR_MSG(3)
#define IPC_MONITOR_SYS_HW_INFO_GET    IPC_MONITOR_MSG(4)
#define IPC_MONITOR_XGE_CARD_INFO_GET  IPC_MONITOR_MSG(5)
#define IPC_MONITOR_CPU_LOAD_GET       IPC_MONITOR_MSG(6)

#define IPC_CLI_MSG_BASE  (IPC_MSG_BASE+1000)/*1100*/
#define IPC_CLI_MSG(i) (IPC_MONITOR_MSG_BASE+i)

#define IPC_CLI_MOD_CFG_RESTORE_REQ     IPC_CLI_MSG(1)
#define IPC_CLI_MOD_CFG_RESTORE_STATUS  IPC_CLI_MSG(2)


#define IPC_QOS_MSG_BASE  (IPC_MSG_BASE+1100)/*1200*/
#define IPC_QOS_MSG(i) (IPC_QOS_MSG_BASE+i)

#define IPC_SWITCH_QOS_QSET_MSG     IPC_QOS_MSG(1)
#define IPC_SWITCH_QOS_ENTRY_MSG    IPC_QOS_MSG(2)
#define IPC_SWITCH_QOS_QUAL_MSG     IPC_QOS_MSG(3)
#define IPC_SWITCH_QOS_GROUP_MSG    IPC_QOS_MSG(4)
#define IPC_SWITCH_QOS_ACTION_MSG   IPC_QOS_MSG(5)
#define IPC_SWITCH_QOS_POLICER_MSG  IPC_QOS_MSG(6)
#define IPC_SWITCH_QOS_STAT_MSG     IPC_QOS_MSG(7)
#define IPC_SWITCH_QOS_COS_MSG      IPC_QOS_MSG(8)
#define IPC_SWITCH_QOS_FP_SHOW_MSG  IPC_QOS_MSG(9)
#define IPC_SWITCH_QOS_FP_SHOW_MSG     IPC_QOS_MSG(9)

#define IPC_DRV_MSG_BASE (IPC_MSG_BASE+1200) /* 1300 */
#define IPC_DRV_MSG(i) (IPC_DRV_MSG_BASE+i)

#define IPC_SWITCH_DRV_ALL_SFP_GE_STATUS_GET    IPC_DRV_MSG(1)
#define IPC_SWITCH_DRV_BOARD_TEMP_GET           IPC_DRV_MSG(2)
#define IPC_SWITCH_DRV_FAN_SPEED_GET            IPC_DRV_MSG(3)
#define IPC_SWITCH_DRV_RTC_SET                  IPC_DRV_MSG(4)
#define IPC_SWITCH_DRV_BOARD_VER_GET            IPC_DRV_MSG(5)
#define IPC_SWITCH_DRV_SFP_GE_ENABLE            IPC_DRV_MSG(6)
#define IPC_SWITCH_DRV_KEEP_ALIVE               IPC_DRV_MSG(7)
#define IPC_SWITCH_DRV_ALARM_LED_SET            IPC_DRV_MSG(8)
#define IPC_SWITCH_DRV_DDM_INFO_GET             IPC_DRV_MSG(9)
#define IPC_SWITCH_DRV_RTC_SYNC                 IPC_DRV_MSG(10)
#define IPC_SWITCH_DRV_TF_ALL_RST              IPC_DRV_MSG(11)
#define IPC_SWITCH_DRV_XGE_DDM_INFO_GET         IPC_DRV_MSG(12)


/* lacp模块ipc消息 */
#define IPC_LACP_MSG_BASE   (IPC_MSG_BASE+1400)     /*1500*/
#define IPC_LACP_MSG(i)     (IPC_LACP_MSG_BASE+i)
#define IPC_SWITCH_LACP_TRUNK_CREATE            IPC_LACP_MSG(1)
#define IPC_SWITCH_LACP_TRUNK_DESTROY           IPC_LACP_MSG(2)
#define IPC_SWITCH_LACP_PORTSTATE_SET           IPC_LACP_MSG(3)
#define IPC_SWITCH_LACP_PORTSTATE_GET           IPC_LACP_MSG(4)
#define IPC_SWITCH_LACP_TRUNK_MEM_ADD           IPC_LACP_MSG(5)
#define IPC_SWITCH_LACP_TRUNK_MEM_DEL           IPC_LACP_MSG(6)
#define IPC_SWITCH_LACP_TRUNK_MEM_ADD_SET       IPC_LACP_MSG(7)
#define IPC_SWITCH_LACP_TRUNK_MEM_DEL_SET       IPC_LACP_MSG(8)
#define IPC_SWITCH_LACP_TRUNK_PSC_SET           IPC_LACP_MSG(9)
#define IPC_SWITCH_LACP_TRUNK_UP_DOWN           IPC_LACP_MSG(10)
#define IPC_SWITCH_LACP_TRUNK_SPEED_CHANGE      IPC_LACP_MSG(11)
#define IPC_SWITCH_LACP_ONE_LACP_UP_PORT_GET    IPC_LACP_MSG(12)  /* 发往lacp获取lacp信息 */
#define IPC_SWITCH_LACP_ALL_LACP_PORT_GET       IPC_LACP_MSG(13)  /* 发往lacp获取lacp信息 */

#if 0
/* 802dot1x模块ipc消息 */
#define IPC_DOT1X_MSG_BASE   (IPC_MSG_BASE+1500)     /*1600*/
#define IPC_DOT1X_MSG(i)     (IPC_DOT1X_MSG_BASE+i)
#define IPC_SWITCH_DOT1X_CREATE            IPC_DOT1X_MSG(1)
#endif

#define IPC_SNMP_MSG_BASE  (IPC_MSG_BASE+1450)/*1550*/
#define IPC_SNMP_MSG(i) (IPC_SNMP_MSG_BASE+i)

#define IPC_PKT_SNMP_CONFIG_SET_MSG      IPC_SNMP_MSG(1)
#define IPC_PKT_SNMP_CONFIG_GET_MSG      IPC_SNMP_MSG(2)
#define IPC_SNMP_CONFIG_GET_MSG          IPC_SNMP_MSG(3)
#define IPC_SNMP_CONFIG_SET_MSG          IPC_SNMP_MSG(4)

#define IPC_BCM_DEBUG_SHELL_MSG IPC_SNMP_MSG(3)

/* 802dot1x模块ipc消息 */
#define IPC_DOT1X_MSG_BASE   (IPC_MSG_BASE+1500)     /*1600*/
#define IPC_DOT1X_MSG(i)     (IPC_DOT1X_MSG_BASE+i)
#define IPC_SWITCH_DOT1X_AUTH_INIT              IPC_DOT1X_MSG(1)
#define IPC_SWITCH_DOT1X_AUTH_DETACH            IPC_DOT1X_MSG(2)
#define IPC_SWITCH_DOT1X_AUTH_MODE_SET          IPC_DOT1X_MSG(3)
#define IPC_SWITCH_DOT1X_AUTH_MODE_GET          IPC_DOT1X_MSG(4)
#define IPC_SWITCH_DOT1X_L2_MAC_ADD             IPC_DOT1X_MSG(5)
#define IPC_SWITCH_DOT1X_L2_MAC_DEL             IPC_DOT1X_MSG(6)
#define IPC_SWITCH_DOT1X_L2_MAC_FLUSH           IPC_DOT1X_MSG(7)
#define IPC_SWITCH_PPPOEPLUS_ENABLE_RECV        IPC_DOT1X_MSG(8)

#define IPC_TF_MSG_BASE  (IPC_MSG_BASE+1600)/*1700*/
#define IPC_TF_MSG(i) (IPC_TF_MSG_BASE+i)

#define IPC_ETF_ONU_ONLINE_FLAG_AND_PORT_NUM_GET IPC_TF_MSG(1)
#define IPC_ETF_ONU_UNI_STATISTICS_CURRENT_MSG_GET IPC_TF_MSG(2)
#define IPC_ETF_ONU_UNI_STATISTICS_CURRENT_MSG_SET IPC_TF_MSG(3)
#define IPC_ETF_ONU_ETH_PORT_INFO_GET IPC_TF_MSG(4)
#define IPC_ETF_ONU_ETH_PORT_VLAN_SET IPC_TF_MSG(5)
#define IPC_ETF_ONU_ETH_PORT_VLAN_DEL IPC_TF_MSG(6)
#define IPC_SWITCH_DHCP_ONU_ID_AND_UNI_ID_GET IPC_TF_MSG(7)
#define IPC_TF_ONT_DEACTIVE_MSG              IPC_TF_MSG(8)

#define IPC_TF_MAC_ADDR_FLUSH_ALL_MSG        IPC_TF_MSG(10)
#define IPC_TF_MAC_ADDR_FLUSH_DYNAMIC_MSG    IPC_TF_MSG(11)
#define IPC_TF_MAC_ADDR_FLUSH_PORT_MSG       IPC_TF_MSG(12)
#define IPC_TF_MAC_ADDR_FLUSH_VLAN_MSG       IPC_TF_MSG(13)
#define IPC_TF_MAC_ADDR_LEARNING_ADMIN       IPC_TF_MSG(14)
#define IPC_TF_MAC_ADDR_AGING_TIME           IPC_TF_MSG(15)

#endif //IPC_PUBLIC_H

