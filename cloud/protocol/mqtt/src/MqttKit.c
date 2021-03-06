
#include "MqttKit.h"
#include "Common.h"
#include "ql_stdlib.h"
#include "mqtt.h"


#define CMD_TOPIC_PREFIX		"$creq"
#include "ql_uart.h"
#define DEBUG_ENABLE 1
#if DEBUG_ENABLE > 0
#define DEBUG_PORT  UART_PORT1
#define DBG_BUF_LEN   512
static char DBG_BUFFER[DBG_BUF_LEN];
#define APP_DEBUG(FORMAT,...) {\
    Ql_memset(DBG_BUFFER, 0, DBG_BUF_LEN);\
    Ql_sprintf(DBG_BUFFER,FORMAT,##__VA_ARGS__); \
    if (UART_PORT2 == (DEBUG_PORT)) \
    {\
        Ql_Debug_Trace(DBG_BUFFER);\
    } else {\
        Ql_UART_Write((Enum_SerialPort)(DEBUG_PORT), (u8*)(DBG_BUFFER), Ql_strlen((const char *)(DBG_BUFFER)));\
    }\
}
#else
#define APP_DEBUG(FORMAT,...) 
#endif



void MQTT_NewBuffer(MQTT_PACKET_STRUCTURE *mqttPacket, uint32 size)
{
	mqttPacket->_data = dataMem;				//全局数组方式
	mqttPacket->_len = 0;
	mqttPacket->_size = MQTTSIZE;		//全局数组方式
	Ql_memset(dataMem,0, MQTTSIZE);
}


void MQTT_DeleteBuffer(MQTT_PACKET_STRUCTURE *mqttPacket)
{


	Ql_memset(mqttPacket->_data,0,MQTTSIZE);
	mqttPacket->_len = 0;
	//mqttPacket->_size = MQTTSIZE;

}

int32 MQTT_DumpLength(size_t len, uint8 *buf)
{
	
	int32 i = 0;
	
	for(i = 1; i <= 4; ++i)
	{
		*buf = len % 128;
		len >>= 7;
		if(len > 0)
		{
			*buf |= 128;
			++buf;
		}
		else
		{
			return i;
		}
	}

	return -1;
}

int32 MQTT_ReadLength(const uint8 *stream, int32 size, uint32 *len)
{
	
	int32 i;
	const uint8 *in = stream;
	uint32 multiplier = 1;

	*len = 0;
	for(i = 0; i < size; ++i)
	{
		*len += (in[i] & 0x7f) * multiplier;

		if(!(in[i] & 0x80))
		{
			return i + 1;
		}

		multiplier <<= 7;
		if(multiplier >= 2097152)		//128 * *128 * *128
		{
			return -2;					// error, out of range
		}
	}

	return -1;							// not complete

}

uint8 MQTT_UnPacketRecv(uint8 *dataPtr)
{
	
	uint8 status = 255;
	uint8 type = dataPtr[0] >> 4;				//���ͼ��
	
	if(type < 1 || type > 14)
		return status;
	
	if(type == MQTT_PKT_PUBLISH)
	{
		uint8 *msgPtr;
		uint32 remain_len = 0;
		
		msgPtr = dataPtr + MQTT_ReadLength(dataPtr + 1, 4, &remain_len) + 1;
		
		if(remain_len < 2 || dataPtr[0] & 0x01)					//retain
			return 255;
		
		if(remain_len < ((uint16)msgPtr[0] << 8 | msgPtr[1]) + 2)
			return 255;
		
		if(Ql_strstr((int8 *)msgPtr + 2, CMD_TOPIC_PREFIX) != NULL)	//����������·�
			status = MQTT_PKT_CMD;
		else
			status = MQTT_PKT_PUBLISH;
	}
	else
		status = type;
	
	return status;

}

uint8 MQTT_PacketConnect(const int8 *user, const int8 *password, const int8 *devid,
						uint16 cTime, uint1 clean_session, uint1 qos,
						const int8 *will_topic, const int8 *will_msg, int32 will_retain,
						MQTT_PACKET_STRUCTURE *mqttPacket)
{
	uint8 flags = 0;
	uint8 will_topic_len = 0;
	uint16 total_len = 15;
	int16 len = 0, devid_len = Ql_strlen(devid);
	
	if(!devid)
		return 1;
	
	total_len += devid_len + 2;
	
	if(clean_session)
	{
		flags |= MQTT_CONNECT_CLEAN_SESSION;
	}
	
	if(will_topic)
	{
		flags |= MQTT_CONNECT_WILL_FLAG;
		will_topic_len = Ql_strlen(will_topic);
		total_len += 4 + will_topic_len + Ql_strlen(will_msg);
	}
	
	switch((unsigned char)qos)
	{
		case MQTT_QOS_LEVEL0:
			flags |= MQTT_CONNECT_WILL_QOS0;							//���һ��
		break;
		
		case MQTT_QOS_LEVEL1:
			flags |= (MQTT_CONNECT_WILL_FLAG | MQTT_CONNECT_WILL_QOS1);	//����һ��
		break;
		
		case MQTT_QOS_LEVEL2:
			flags |= (MQTT_CONNECT_WILL_FLAG | MQTT_CONNECT_WILL_QOS2);	//ֻ��һ��
		break;
		
		default:
		return 2;
	}
	
	if(will_retain)
	{
		flags |= (MQTT_CONNECT_WILL_FLAG | MQTT_CONNECT_WILL_RETAIN);
	}
	
	if(!user || !password)
	{
		return 3;
	}
	flags |= MQTT_CONNECT_USER_NAME | MQTT_CONNECT_PASSORD;
	
	total_len += Ql_strlen(user) + Ql_strlen(password) + 4;
	
	//MQTT_NewBuffer(mqttPacket, total_len);
	if(mqttPacket->_data == NULL)
		return 4;
	
	Ql_memset(mqttPacket->_data, 0, total_len);
	mqttPacket->_data[mqttPacket->_len++] = MQTT_PKT_CONNECT << 4;
	
	//�̶�ͷ��----------------------ʣ�೤��ֵ-----------------------------------------------
	len = MQTT_DumpLength(total_len - 5, mqttPacket->_data + mqttPacket->_len);
	if(len < 0)
	{
		MQTT_DeleteBuffer(mqttPacket);
		return 5;
	}
	else
		mqttPacket->_len += len;
	
/*************************************�ɱ�ͷ��***********************************************/
	
	//�ɱ�ͷ��----------------------Э�������� �� Э����--------------------------------------
	mqttPacket->_data[mqttPacket->_len++] = 0;
	mqttPacket->_data[mqttPacket->_len++] = 4;
	mqttPacket->_data[mqttPacket->_len++] = 'M';
	mqttPacket->_data[mqttPacket->_len++] = 'Q';
	mqttPacket->_data[mqttPacket->_len++] = 'T';
	mqttPacket->_data[mqttPacket->_len++] = 'T';
	
	//�ɱ�ͷ��----------------------protocol level 4-----------------------------------------
	mqttPacket->_data[mqttPacket->_len++] = 4;
	
	//�ɱ�ͷ��----------------------���ӱ�־(�ú�����ͷ����������)-----------------------------
    mqttPacket->_data[mqttPacket->_len++] = flags;
	
	//�ɱ�ͷ��----------------------�������ӵ�ʱ��(��)----------------------------------------
	mqttPacket->_data[mqttPacket->_len++] = MOSQ_MSB(cTime);
	mqttPacket->_data[mqttPacket->_len++] = MOSQ_LSB(cTime);
	 
/*************************************��Ϣ��************************************************/

	//��Ϣ��----------------------------devid���ȡ�devid-------------------------------------
	mqttPacket->_data[mqttPacket->_len++] = MOSQ_MSB(devid_len);
	mqttPacket->_data[mqttPacket->_len++] = MOSQ_LSB(devid_len);
	
	Ql_strncat((int8 *)mqttPacket->_data + mqttPacket->_len, devid, devid_len);
	mqttPacket->_len += devid_len;
	
	//��Ϣ��----------------------------will_flag �� will_msg---------------------------------
	if(flags & MQTT_CONNECT_WILL_FLAG)
	{
		unsigned short mLen = 0;
		
		if(!will_msg)
			will_msg = "";
		
		mLen = Ql_strlen(will_topic);
		mqttPacket->_data[mqttPacket->_len++] = MOSQ_MSB(mLen);
		mqttPacket->_data[mqttPacket->_len++] = MOSQ_LSB(mLen);
		Ql_strncat((int8 *)mqttPacket->_data + mqttPacket->_len, will_topic, mLen);
		mqttPacket->_len += mLen;
		
		mLen = Ql_strlen(will_msg);
		mqttPacket->_data[mqttPacket->_len++] = MOSQ_MSB(mLen);
		mqttPacket->_data[mqttPacket->_len++] = MOSQ_LSB(mLen);
		Ql_strncat((int8 *)mqttPacket->_data + mqttPacket->_len, will_msg, mLen);
		mqttPacket->_len += mLen;
	}
	
	//��Ϣ��----------------------------use---------------------------------------------------
	if(flags & MQTT_CONNECT_USER_NAME)
	{
		unsigned short user_len = Ql_strlen(user);
		
		mqttPacket->_data[mqttPacket->_len++] = MOSQ_MSB(user_len);
		mqttPacket->_data[mqttPacket->_len++] = MOSQ_LSB(user_len);
		Ql_strncat((int8 *)mqttPacket->_data + mqttPacket->_len, user, user_len);
		mqttPacket->_len += user_len;
	}

	//��Ϣ��----------------------------password----------------------------------------------
	if(flags & MQTT_CONNECT_PASSORD)
	{
		unsigned short psw_len = Ql_strlen(password);
		
		mqttPacket->_data[mqttPacket->_len++] = MOSQ_MSB(psw_len);
		mqttPacket->_data[mqttPacket->_len++] = MOSQ_LSB(psw_len);
		Ql_strncat((int8 *)mqttPacket->_data + mqttPacket->_len, password, psw_len);
		mqttPacket->_len += psw_len;
	}
	return 0;

}


uint1 MQTT_PacketDisConnect(MQTT_PACKET_STRUCTURE *mqttPacket)
{

	//MQTT_NewBuffer(mqttPacket, 2);
	if(mqttPacket->_data == NULL)
		return 1;
	
/*************************************�̶�ͷ��***********************************************/
	
	//�̶�ͷ��----------------------ͷ����Ϣ-------------------------------------------------
	mqttPacket->_data[mqttPacket->_len++] = MQTT_PKT_DISCONNECT << 4;
	
	//�̶�ͷ��----------------------ʣ�೤��ֵ-----------------------------------------------
	mqttPacket->_data[mqttPacket->_len++] = 0;
	
	return 0;

}

//==========================================================
//	�������ƣ�	MQTT_UnPacketConnectAck
//
//	�������ܣ�	������Ϣ���
//
//	��ڲ�����	rev_data�����յ�����
//
//	���ز�����	1��255-ʧ��		����-ƽ̨�ķ�����
//
//	˵����		
//==========================================================
uint8 MQTT_UnPacketConnectAck(uint8 *rev_data)
{

	if(rev_data[1] != 2)
		return 1;
	
	if(rev_data[2] == 0 || rev_data[2] == 1)
		return rev_data[3];
	else
		return 255;

}

//==========================================================
//	�������ƣ�	MQTT_PacketSaveData
//
//	�������ܣ�	���ݵ��ϴ����
//
//	��ڲ�����	devid���豸ID(��Ϊ��)
//				send_buf��json����buf
//				send_len��json�ܳ�
//				type_bin_head��bin�ļ�����Ϣͷ
//				type������
//
//	���ز�����	0-�ɹ�		1-ʧ��
//
//	˵����		
//==========================================================
uint1 MQTT_PacketSaveData(const int8 *devid, int16 send_len, int8 *type_bin_head, uint8 type, MQTT_PACKET_STRUCTURE *mqttPacket)
{

	if(MQTT_PacketPublish(MQTT_PUBLISH_ID, "$dp", NULL, send_len + 3, MQTT_QOS_LEVEL1, 0, 1, mqttPacket) == 0)
	{
		mqttPacket->_data[mqttPacket->_len++] = type;					//����
		
		mqttPacket->_data[mqttPacket->_len++] = MOSQ_MSB(send_len);
		mqttPacket->_data[mqttPacket->_len++] = MOSQ_LSB(send_len);
	}
	else
		return 1;
	
	return 0;

}

//==========================================================
//	�������ƣ�	MQTT_UnPacketCmd
//
//	�������ܣ�	�����·����
//
//	��ڲ�����	rev_data�����յ�����ָ��
//				cmdid��cmdid-uuid
//				req������
//
//	���ز�����	0-�ɹ�		����-ʧ��ԭ��
//
//	˵����		
//==========================================================
uint8 MQTT_UnPacketCmd(uint8 *rev_data, int8 **cmdid, int8 **req, uint16 *req_len)
{

	int8 *dataPtr = Ql_strchr((int8 *)rev_data + 3, '/');	//��3������ͷ��Ϣ
	
	uint32 remain_len = 0;
	
	if(dataPtr == NULL)									//δ�ҵ�'/'
		return 1;
	dataPtr++;											//����'/'
	
	MQTT_ReadLength(rev_data + 1, 4, &remain_len);		//��ȡʣ���ֽ�
	
	*cmdid = (int8 *)MQTT_MallocBuffer(37);				//cmdid�̶�36�ֽڣ������һ����������λ��
	if(*cmdid == NULL)
		return 2;
	
	Ql_memset(*cmdid, 0, 37);								//ȫ������
	Ql_memcpy(*cmdid, (const int8 *)dataPtr, 36);			//����cmdid
	dataPtr += 36;
	
	*req_len = remain_len - 44;							//����� = ʣ�೤��(remain_len) - 2 - 5($creq) - 1(\) - cmdid����
	*req = (int8 *)MQTT_MallocBuffer(*req_len + 1);		//���������+1
	if(*req == NULL)
	{
		MQTT_FreeBuffer(*cmdid);
		return 3;
	}
	
	Ql_memset(*req, 0, *req_len + 1);						//����
	Ql_memcpy(*req, (const int8 *)dataPtr, *req_len);		//��������
	
	return 0;

}

//==========================================================
//	�������ƣ�	MQTT_PacketCmdResp
//
//	�������ܣ�	����ظ����
//
//	��ڲ�����	cmdid��cmdid
//				req������
//				mqttPacket����ָ��
//
//	���ز�����	0-�ɹ�		1-ʧ��
//
//	˵����		
//==========================================================
uint1 MQTT_PacketCmdResp(const int8 *cmdid, const int8 *req, MQTT_PACKET_STRUCTURE *mqttPacket)
{
	
	uint16 cmdid_len = Ql_strlen(cmdid);
	uint16 req_len = Ql_strlen(req);
	_Bool status = 0;
	
	int8 *payload = MQTT_MallocBuffer(cmdid_len + 6);
	if(payload == NULL)
		return 1;
	
	Ql_memset(payload, 0, cmdid_len + 6);
	Ql_memcpy(payload, "$crsp/", 6);
	Ql_strncat(payload, cmdid, cmdid_len);

	if(MQTT_PacketPublish(MQTT_PUBLISH_ID, payload, req, Ql_strlen(req), MQTT_QOS_LEVEL0, 0, 1, mqttPacket) == 0)
		status = 0;
	else
		status = 1;
	
	MQTT_FreeBuffer(payload);
	
	return status;

}

//==========================================================
//	�������ƣ�	MQTT_PacketSubscribe
//
//	�������ܣ�	Subscribe��Ϣ���
//
//	��ڲ�����	pkt_id��pkt_id
//				qos����Ϣ�ط�����
//				topics�����ĵ���Ϣ
//				topics_cnt�����ĵ���Ϣ����
//				mqttPacket����ָ��
//
//	���ز�����	0-�ɹ�		����-ʧ��
//
//	˵����		
//==========================================================
uint8 MQTT_PacketSubscribe(uint16 pkt_id, enum MqttQosLevel qos, const int8 *topics[], uint8 topics_cnt, MQTT_PACKET_STRUCTURE *mqttPacket)
{
	
	uint32 topic_len = 0, remain_len = 0;
	int16 len = 0;
	uint8 i = 0;
	
	if(pkt_id == 0)
		return 1;
	
	//����topic����-------------------------------------------------------------------------
	for(; i < topics_cnt; i++)
	{
		if(topics[i] == NULL)
			return 2;
		
		topic_len += Ql_strlen(topics[i]);
	}
	
	//2 bytes packet id + topic filter(2 bytes topic + topic length + 1 byte reserve)------
	remain_len = 2 + 3 * topics_cnt + topic_len;
	
	//�����ڴ�------------------------------------------------------------------------------
	//MQTT_NewBuffer(mqttPacket, remain_len + 5);
	if(mqttPacket->_data == NULL)
		return 3;
	
/*************************************�̶�ͷ��***********************************************/
	
	//�̶�ͷ��----------------------ͷ����Ϣ-------------------------------------------------
	mqttPacket->_data[mqttPacket->_len++] = MQTT_PKT_SUBSCRIBE << 4 | 0x02;
	
	//�̶�ͷ��----------------------ʣ�೤��ֵ-----------------------------------------------
	len = MQTT_DumpLength(remain_len, mqttPacket->_data + mqttPacket->_len);
	if(len < 0)
	{
		MQTT_DeleteBuffer(mqttPacket);
		return 4;
	}
	else
		mqttPacket->_len += len;
	
/*************************************payload***********************************************/
	
	//payload----------------------pkt_id---------------------------------------------------
	mqttPacket->_data[mqttPacket->_len++] = MOSQ_MSB(pkt_id);
	mqttPacket->_data[mqttPacket->_len++] = MOSQ_LSB(pkt_id);
	
	//payload----------------------topic_name-----------------------------------------------
	for(i = 0; i < topics_cnt; i++)
	{
		topic_len = Ql_strlen(topics[i]);
		mqttPacket->_data[mqttPacket->_len++] = MOSQ_MSB(topic_len);
		mqttPacket->_data[mqttPacket->_len++] = MOSQ_LSB(topic_len);
		
		Ql_strncat((int8 *)mqttPacket->_data + mqttPacket->_len, topics[i], topic_len);
		mqttPacket->_len += topic_len;
		
		mqttPacket->_data[mqttPacket->_len++] = qos & 0xFF;
	}

	return 0;

}

//==========================================================
//	�������ƣ�	MQTT_UnPacketSubscrebe
//
//	�������ܣ�	Subscribe�Ļظ���Ϣ���
//
//	��ڲ�����	rev_data�����յ�����Ϣ
//
//	���ز�����	0-�ɹ�		����-ʧ��
//
//	˵����		
//==========================================================
uint8 MQTT_UnPacketSubscribe(uint8 *rev_data)
{
	
	uint8 result = 255;

	if(rev_data[2] == MOSQ_MSB(MQTT_SUBSCRIBE_ID) && rev_data[3] == MOSQ_LSB(MQTT_SUBSCRIBE_ID))
	{
		switch(rev_data[4])
		{
			case 0x00:
			case 0x01:
			case 0x02:
				//MQTT Subscribe OK
				result = 0;
			break;
			
			case 0x80:
				//MQTT Subscribe Failed
				result = 1;
			break;
			
			default:
				//MQTT Subscribe UnKnown Err
				result = 2;
			break;
		}
	}
	
	return result;

}

//==========================================================
//	�������ƣ�	MQTT_PacketUnSubscribe
//
//	�������ܣ�	UnSubscribe��Ϣ���
//
//	��ڲ�����	pkt_id��pkt_id
//				qos����Ϣ�ط�����
//				topics�����ĵ���Ϣ
//				topics_cnt�����ĵ���Ϣ����
//				mqttPacket����ָ��
//
//	���ز�����	0-�ɹ�		����-ʧ��
//
//	˵����		
//==========================================================
uint8 MQTT_PacketUnSubscribe(uint16 pkt_id, const int8 *topics[], uint8 topics_cnt, MQTT_PACKET_STRUCTURE *mqttPacket)
{
	
	uint32 topic_len = 0, remain_len = 0;
	int16 len = 0;
	uint8 i = 0;
	
	if(pkt_id == 0)
		return 1;
	
	//����topic����-------------------------------------------------------------------------
	for(; i < topics_cnt; i++)
	{
		if(topics[i] == NULL)
			return 2;
		
		topic_len += Ql_strlen(topics[i]);
	}
	
	//2 bytes packet id, 2 bytes topic length + topic + 1 byte reserve---------------------
	remain_len = 2 + (topics_cnt << 1) + topic_len;
	
	//�����ڴ�------------------------------------------------------------------------------
	//MQTT_NewBuffer(mqttPacket, remain_len + 5);
	if(mqttPacket->_data == NULL)
		return 3;
	
/*************************************�̶�ͷ��***********************************************/
	
	//�̶�ͷ��----------------------ͷ����Ϣ-------------------------------------------------
	mqttPacket->_data[mqttPacket->_len++] = MQTT_PKT_UNSUBSCRIBE << 4 | 0x02;
	
	//�̶�ͷ��----------------------ʣ�೤��ֵ-----------------------------------------------
	len = MQTT_DumpLength(remain_len, mqttPacket->_data + mqttPacket->_len);
	if(len < 0)
	{
		MQTT_DeleteBuffer(mqttPacket);
		return 4;
	}
	else
		mqttPacket->_len += len;
	
/*************************************payload***********************************************/
	
	//payload----------------------pkt_id---------------------------------------------------
	mqttPacket->_data[mqttPacket->_len++] = MOSQ_MSB(pkt_id);
	mqttPacket->_data[mqttPacket->_len++] = MOSQ_LSB(pkt_id);
	
	//payload----------------------topic_name-----------------------------------------------
	for(i = 0; i < topics_cnt; i++)
	{
		topic_len = Ql_strlen(topics[i]);
		mqttPacket->_data[mqttPacket->_len++] = MOSQ_MSB(topic_len);
		mqttPacket->_data[mqttPacket->_len++] = MOSQ_LSB(topic_len);
		
		Ql_strncat((int8 *)mqttPacket->_data + mqttPacket->_len, topics[i], topic_len);
		mqttPacket->_len += topic_len;
	}

	return 0;

}

//==========================================================
//	�������ƣ�	MQTT_UnPacketUnSubscribe
//
//	�������ܣ�	UnSubscribe�Ļظ���Ϣ���
//
//	��ڲ�����	rev_data�����յ�����Ϣ
//
//	���ز�����	0-�ɹ�		����-ʧ��
//
//	˵����		
//==========================================================
uint1 MQTT_UnPacketUnSubscribe(uint8 *rev_data)
{
	
	uint1 result = 1;

	if(rev_data[2] == MOSQ_MSB(MQTT_UNSUBSCRIBE_ID) && rev_data[3] == MOSQ_LSB(MQTT_UNSUBSCRIBE_ID))
	{
		result = 0;
	}
	
	return result;

}

//==========================================================
//	�������ƣ�	MQTT_PacketPublish
//
//	�������ܣ�	Pulish��Ϣ���
//
//	��ڲ�����	pkt_id��pkt_id
//				topic��������topic
//				payload����Ϣ��
//				payload_len����Ϣ�峤��
//				qos���ط�����
//				retain��������Ϣ����
//				own��
//				mqttPacket����ָ��
//
//	���ز�����	0-�ɹ�		����-ʧ��
//
//	˵����		
//==========================================================
uint8 MQTT_PacketPublish(uint16 pkt_id, const int8 *topic,
						const int8 *payload, uint32 payload_len,
						enum MqttQosLevel qos, int32 retain, int32 own,
						MQTT_PACKET_STRUCTURE *mqttPacket)
{

	uint32 total_len = 0, topic_len = 0;
	int32 len = 0;
	uint8 flags = 0;
	
	//pkt_id���----------------------------------------------------------------------------
	if(pkt_id == 0)
		return 1;
	
	//$dpΪϵͳ�ϴ����ݵ��ָ��--------------------------------------------------------------
	for(topic_len = 0; topic[topic_len] != '\0'; ++topic_len)
	{
		if((topic[topic_len] == '#') || (topic[topic_len] == '+'))
			return 2;
	}
	
	//Publish��Ϣ---------------------------------------------------------------------------
	flags |= MQTT_PKT_PUBLISH << 4;
	
	//retain��־----------------------------------------------------------------------------
	if(retain)
		flags |= 0x01;
	
	//�ܳ���--------------------------------------------------------------------------------
	total_len = topic_len + payload_len + 2;
	
	//qos����--��Ҫ����PUBLISH������̬����Ϣ�ģ���֤��Ϣ���ݵĴ���-----------------------------
	switch(qos)
	{
		case MQTT_QOS_LEVEL0:
			flags |= MQTT_CONNECT_WILL_QOS0;	//���һ��
		break;
		
		case MQTT_QOS_LEVEL1:
			flags |= 0x02;						//����һ��
			total_len += 2;
		break;
		
		case MQTT_QOS_LEVEL2:
			flags |= 0x04;						//ֻ��һ��
			total_len += 2;
		break;
		
		default:
		return 3;
	}
	
	//�����ڴ�------------------------------------------------------------------------------
	//MQTT_NewBuffer(mqttPacket, total_len + 3);
	if(mqttPacket->_data == NULL)
		return 4;
		
	Ql_memset(mqttPacket->_data, 0, total_len + 3);
	
/*************************************�̶�ͷ��***********************************************/
	
	//�̶�ͷ��----------------------ͷ����Ϣ-------------------------------------------------
	mqttPacket->_data[mqttPacket->_len++] = flags;
	
	//�̶�ͷ��----------------------ʣ�೤��ֵ-----------------------------------------------
	len = MQTT_DumpLength(total_len, mqttPacket->_data + mqttPacket->_len);
	if(len < 0)
	{
		MQTT_DeleteBuffer(mqttPacket);
		return 5;
	}
	else
		mqttPacket->_len += len;
	
/*************************************�ɱ�ͷ��***********************************************/
	
	//�ɱ�ͷ��----------------------д��topic���ȡ�topic-------------------------------------
	mqttPacket->_data[mqttPacket->_len++] = MOSQ_MSB(topic_len);
	mqttPacket->_data[mqttPacket->_len++] = MOSQ_LSB(topic_len);
	
	Ql_strncat((int8 *)mqttPacket->_data + mqttPacket->_len, topic, topic_len);
	mqttPacket->_len += topic_len;
	if(qos != MQTT_QOS_LEVEL0)
	{
		mqttPacket->_data[mqttPacket->_len++] = MOSQ_MSB(pkt_id);
		mqttPacket->_data[mqttPacket->_len++] = MOSQ_LSB(pkt_id);
	}
	
	//�ɱ�ͷ��----------------------д��payload----------------------------------------------
	if(payload != NULL)
	{
		Ql_strncat((int8 *)mqttPacket->_data + mqttPacket->_len, payload, payload_len);
		mqttPacket->_len += payload_len;
	}
	
	return 0;

}

//==========================================================
//	�������ƣ�	MQTT_UnPacketPublish
//
//	�������ܣ�	Publish��Ϣ���
//
//	��ڲ�����	flags��MQTT��ر�־��Ϣ
//				pkt��ָ��ɱ�ͷ��
//				size���̶�ͷ���е�ʣ�೤����Ϣ
//
//	���ز�����	0-�ɹ�		����-ʧ��ԭ��
//
//	˵����		
//==========================================================
uint8 MQTT_UnPacketPublish(uint8 *rev_data, int8 **topic, uint16 *topic_len, int8 **payload, uint16 *payload_len, uint8 *qos, uint16 *pkt_id)
{
	
	const int8 flags = rev_data[0] & 0x0F;
	uint8 *msgPtr;
	uint32 remain_len = 0;

	const int8 dup = flags & 0x08;

	*qos = (flags & 0x06) >> 1;
	
	msgPtr = rev_data + MQTT_ReadLength(rev_data + 1, 4, &remain_len) + 1;
	
	if(remain_len < 2 || flags & 0x01)							//retain
		return 255;
	
	*topic_len = (uint16)msgPtr[0] << 8 | msgPtr[1];
	if(remain_len < *topic_len + 2)
		return 255;
	
	if(Ql_strstr((int8 *)msgPtr + 2, CMD_TOPIC_PREFIX) != NULL)	//����������·�
		return MQTT_PKT_CMD;
	
	switch(*qos)
	{
		case MQTT_QOS_LEVEL0:									// qos0 have no packet identifier
			
			if(0 != dup)
				return 255;

			*topic = MQTT_MallocBuffer(*topic_len + 1);			//Ϊtopic�����ڴ�
			if(*topic == NULL)
				return 255;
			
			Ql_memset(*topic, 0, *topic_len + 1);
			Ql_memcpy(*topic, (int8 *)msgPtr + 2, *topic_len);		//��������
			
			*payload_len = remain_len - 2 - *topic_len;			//Ϊpayload�����ڴ�
			*payload = MQTT_MallocBuffer(*payload_len + 1);
			if(*payload == NULL)								//���ʧ��
			{
				MQTT_FreeBuffer(*topic);						//����Ҫ��topic���ڴ��ͷŵ�
				return 255;
			}
			
			Ql_memset(*payload, 0, *payload_len + 1);
			Ql_memcpy(*payload, (int8 *)msgPtr + 2 + *topic_len, *payload_len);
			
		break;

		case MQTT_QOS_LEVEL1:
		case MQTT_QOS_LEVEL2:
			
			if(*topic_len + 2 > remain_len)
				return 255;
			
			*pkt_id = (uint16)msgPtr[*topic_len + 2] << 8 | msgPtr[*topic_len + 3];
			if(pkt_id == 0)
				return 255;
			
			*topic = MQTT_MallocBuffer(*topic_len + 1);			//Ϊtopic�����ڴ�
			if(*topic == NULL)
				return 255;
			
			Ql_memset(*topic, 0, *topic_len + 1);
			Ql_memcpy(*topic, (int8 *)msgPtr + 2, *topic_len);		//��������
			
			*payload_len = remain_len - 4 - *topic_len;
			*payload = MQTT_MallocBuffer(*payload_len + 1);		//Ϊpayload�����ڴ�
			if(*payload == NULL)								//���ʧ��
			{
				MQTT_FreeBuffer(*topic);						//����Ҫ��topic���ڴ��ͷŵ�
				return 255;
			}
			
			Ql_memset(*payload, 0, *payload_len + 1);
			Ql_memcpy(*payload, (int8 *)msgPtr + 4 + *topic_len, *payload_len);
			
		break;

		default:
			return 255;
	}
	
	if(Ql_strchr((int8 *)topic, '+') || Ql_strchr((int8 *)topic, '#'))
		return 255;

	return 0;

}

//==========================================================
//	�������ƣ�	MQTT_PacketPublishAck
//
//	�������ܣ�	Publish Ack��Ϣ���
//
//	��ڲ�����	pkt_id��packet id
//				mqttPacket����ָ��
//
//	���ز�����	0-�ɹ�		1-ʧ��ԭ��
//
//	˵����		���յ���Publish��Ϣ��QoS�ȼ�Ϊ1ʱ����ҪAck�ظ�
//==========================================================
uint1 MQTT_PacketPublishAck(uint16 pkt_id, MQTT_PACKET_STRUCTURE *mqttPacket)
{

	//MQTT_NewBuffer(mqttPacket, 4);
	if(mqttPacket->_data == NULL)
		return 1;
	
/*************************************�̶�ͷ��***********************************************/
	
	//�̶�ͷ��----------------------ͷ����Ϣ-------------------------------------------------
	mqttPacket->_data[mqttPacket->_len++] = MQTT_PKT_PUBACK << 4;
	
	//�̶�ͷ��----------------------ʣ�೤��-------------------------------------------------
	mqttPacket->_data[mqttPacket->_len++] = 2;
	
/*************************************�ɱ�ͷ��***********************************************/
	
	//�ɱ�ͷ��----------------------pkt_id����-----------------------------------------------
	mqttPacket->_data[mqttPacket->_len++] = pkt_id >> 8;
	mqttPacket->_data[mqttPacket->_len++] = pkt_id & 0xff;
	
	return 0;

}

//==========================================================
//	�������ƣ�	MQTT_UnPacketPublishAck
//
//	�������ܣ�	Publish Ack��Ϣ���
//
//	��ڲ�����	rev_data���յ�������
//
//	���ز�����	0-�ɹ�		1-ʧ��ԭ��
//
//	˵����		
//==========================================================
uint1 MQTT_UnPacketPublishAck(uint8 *rev_data)
{

	if(rev_data[1] != 2)
		return 1;

	if(rev_data[2] == MOSQ_MSB(MQTT_PUBLISH_ID) && rev_data[3] == MOSQ_LSB(MQTT_PUBLISH_ID))
		return 0;
	else
		return 1;

}

//==========================================================
//	�������ƣ�	MQTT_PacketPublishRec
//
//	�������ܣ�	Publish Rec��Ϣ���
//
//	��ڲ�����	pkt_id��packet id
//				mqttPacket����ָ��
//
//	���ز�����	0-�ɹ�		1-ʧ��ԭ��
//
//	˵����		���յ���Publish��Ϣ��QoS�ȼ�Ϊ2ʱ�����յ�rec
//==========================================================
uint1 MQTT_PacketPublishRec(uint16 pkt_id, MQTT_PACKET_STRUCTURE *mqttPacket)
{

	//MQTT_NewBuffer(mqttPacket, 4);
	if(mqttPacket->_data == NULL)
		return 1;
	
/*************************************�̶�ͷ��***********************************************/
	
	//�̶�ͷ��----------------------ͷ����Ϣ-------------------------------------------------
	mqttPacket->_data[mqttPacket->_len++] = MQTT_PKT_PUBREC << 4;
	
	//�̶�ͷ��----------------------ʣ�೤��-------------------------------------------------
	mqttPacket->_data[mqttPacket->_len++] = 2;
	
/*************************************�ɱ�ͷ��***********************************************/
	
	//�ɱ�ͷ��----------------------pkt_id����-----------------------------------------------
	mqttPacket->_data[mqttPacket->_len++] = pkt_id >> 8;
	mqttPacket->_data[mqttPacket->_len++] = pkt_id & 0xff;
	
	return 0;

}

//==========================================================
//	�������ƣ�	MQTT_UnPacketPublishRec
//
//	�������ܣ�	Publish Rec��Ϣ���
//
//	��ڲ�����	rev_data�����յ�������
//
//	���ز�����	0-�ɹ�		1-ʧ��
//
//	˵����		
//==========================================================
uint1 MQTT_UnPacketPublishRec(uint8 *rev_data)
{

	if(rev_data[1] != 2)
		return 1;

	if(rev_data[2] == MOSQ_MSB(MQTT_PUBLISH_ID) && rev_data[3] == MOSQ_LSB(MQTT_PUBLISH_ID))
		return 0;
	else
		return 1;

}

//==========================================================
//	�������ƣ�	MQTT_PacketPublishRel
//
//	�������ܣ�	Publish Rel��Ϣ���
//
//	��ڲ�����	pkt_id��packet id
//				mqttPacket����ָ��
//
//	���ز�����	0-�ɹ�		1-ʧ��ԭ��
//
//	˵����		���յ���Publish��Ϣ��QoS�ȼ�Ϊ2ʱ�����յ�rec���ٻظ�rel
//==========================================================
uint1 MQTT_PacketPublishRel(uint16 pkt_id, MQTT_PACKET_STRUCTURE *mqttPacket)
{

	//MQTT_NewBuffer(mqttPacket, 4);
	if(mqttPacket->_data == NULL)
		return 1;
	
/*************************************�̶�ͷ��***********************************************/
	
	//�̶�ͷ��----------------------ͷ����Ϣ-------------------------------------------------
	mqttPacket->_data[mqttPacket->_len++] = MQTT_PKT_PUBREL << 4 | 0x02;
	
	//�̶�ͷ��----------------------ʣ�೤��-------------------------------------------------
	mqttPacket->_data[mqttPacket->_len++] = 2;
	
/*************************************�ɱ�ͷ��***********************************************/
	
	//�ɱ�ͷ��----------------------pkt_id����-----------------------------------------------
	mqttPacket->_data[mqttPacket->_len++] = pkt_id >> 8;
	mqttPacket->_data[mqttPacket->_len++] = pkt_id & 0xff;
	
	return 0;

}

//==========================================================
//	�������ƣ�	MQTT_UnPacketPublishRel
//
//	�������ܣ�	Publish Rel��Ϣ���
//
//	��ڲ�����	rev_data�����յ�������
//
//	���ز�����	0-�ɹ�		1-ʧ��
//
//	˵����		
//==========================================================
uint1 MQTT_UnPacketPublishRel(uint8 *rev_data, uint16 pkt_id)
{

	if(rev_data[1] != 2)
		return 1;

	if(rev_data[2] == MOSQ_MSB(pkt_id) && rev_data[3] == MOSQ_LSB(pkt_id))
		return 0;
	else
		return 1;

}

//==========================================================
//	�������ƣ�	MQTT_PacketPublishComp
//
//	�������ܣ�	Publish Comp��Ϣ���
//
//	��ڲ�����	pkt_id��packet id
//				mqttPacket����ָ��
//
//	���ز�����	0-�ɹ�		1-ʧ��ԭ��
//
//	˵����		���յ���Publish��Ϣ��QoS�ȼ�Ϊ2ʱ�����յ�rec���ٻظ�rel
//==========================================================
uint1 MQTT_PacketPublishComp(uint16 pkt_id, MQTT_PACKET_STRUCTURE *mqttPacket)
{

	//MQTT_NewBuffer(mqttPacket, 4);
	if(mqttPacket->_data == NULL)
		return 1;
	
/*************************************�̶�ͷ��***********************************************/
	
	//�̶�ͷ��----------------------ͷ����Ϣ-------------------------------------------------
	mqttPacket->_data[mqttPacket->_len++] = MQTT_PKT_PUBCOMP << 4;
	
	//�̶�ͷ��----------------------ʣ�೤��-------------------------------------------------
	mqttPacket->_data[mqttPacket->_len++] = 2;
	
/*************************************�ɱ�ͷ��***********************************************/
	
	//�ɱ�ͷ��----------------------pkt_id����-----------------------------------------------
	mqttPacket->_data[mqttPacket->_len++] = pkt_id >> 8;
	mqttPacket->_data[mqttPacket->_len++] = pkt_id & 0xff;
	
	return 0;

}

//==========================================================
//	�������ƣ�	MQTT_UnPacketPublishComp
//
//	�������ܣ�	Publish Comp��Ϣ���
//
//	��ڲ�����	rev_data�����յ�������
//
//	���ز�����	0-�ɹ�		1-ʧ��
//
//	˵����		
//==========================================================
uint1 MQTT_UnPacketPublishComp(uint8 *rev_data)
{

	if(rev_data[1] != 2)
		return 1;

	if(rev_data[2] == MOSQ_MSB(MQTT_PUBLISH_ID) && rev_data[3] == MOSQ_LSB(MQTT_PUBLISH_ID))
		return 0;
	else
		return 1;

}

uint1 MQTT_PacketPing(MQTT_PACKET_STRUCTURE *mqttPacket)
{

	MQTT_NewBuffer(mqttPacket, 2);
	if(mqttPacket->_data == NULL)
		return 1;
	
/*************************************固定头部***********************************************/
	
	//固定头部----------------------头部消息-------------------------------------------------
	mqttPacket->_data[mqttPacket->_len++] = MQTT_PKT_PINGREQ << 4;
	
	//固定头部----------------------剩余长度-------------------------------------------------
	mqttPacket->_data[mqttPacket->_len++] = 0;
	
	return 0;

}


