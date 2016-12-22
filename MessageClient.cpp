#include "MessageClient.h"

#include <QByteArray>
#include <QMetaType>
#include <QDebug>
MessageClient::MessageClient(char id,uint port,const QString& server,QObject* parent) : QObject(parent)
{
    m_ClientID = '\0';
    if( (id>='A'&&id<='Z') || (id>='a'&&id<='z'))
    {
        m_Port = port;
        m_Server = server;
        m_ClientID = id;
        m_TcpSocket = new QTcpSocket(this);
        m_TcpSocket->setSocketOption(QAbstractSocket::KeepAliveOption,1);//立即发送;

        //注册特殊类型;
        qRegisterMetaType<QAbstractSocket::SocketState>("QAbstractSocket::SocketState");
        qRegisterMetaType<QAbstractSocket::SocketError>("QAbstractSocket::SocketError");

        connect(m_TcpSocket,SIGNAL(stateChanged(QAbstractSocket::SocketState)),this,SLOT(StateChangedSlot(QAbstractSocket::SocketState)));
        connect(m_TcpSocket,SIGNAL(readyRead()),this,SLOT(ReadyReadSlot()));
        connect(m_TcpSocket,SIGNAL(error(QAbstractSocket::SocketError)),this,SLOT(GetErrorMsgSlot(QAbstractSocket::SocketError)));
    }
}

MessageClient::~MessageClient()
{
    if(m_TcpSocket->isValid())
    {
        m_TcpSocket->close();
        m_TcpSocket->deleteLater();
    }
  //  qDebug() << "Client: " << m_ClientID << " disconnect!" << endl;
}

void MessageClient::StartSlot()
{
    this->Start();
}

void MessageClient::StopSlot()
{
    this->Stop();
}

void MessageClient::BroadcastSlot(QByteArray data)
{
    this->Broadcast(data);
}

void MessageClient::UnicastSlot(char destClient,QByteArray data)
{
    this->Unicast(destClient,data);
}

void MessageClient::ReqDestClientStateSlot(char destClient)
{
    this->ReqDestClientState(destClient);
}

void MessageClient::ReqClientListSlot()
{
    this->ReqClientList();
}

void MessageClient::CloseClientSlot()
{
    if(m_TcpSocket->isValid())
    {
     //   m_TcpSocket->close();
        m_TcpSocket->abort();
        m_TcpSocket->deleteLater();
    }
    this->destroyed(this);
}

void MessageClient::ReadyReadSlot()
{
    QByteArray bytes = m_TcpSocket->readAll();

    //未收到数据，直接返回;
    if(bytes.length()<=0)
        return ;

    //将上次剩余的数据拼接在最前面;
    if(m_RecvSurplus.length()>0)
    {
        bytes.push_front(m_RecvSurplus);
    }

    while(bytes.length()>=5) //有意义的数据包至少5个字节;
    {
        //收到的消息为非法消息，直接丢弃(返回);
        if(bytes[0]!='@') //首字符必须为'@'，否则被视为非法消息;
        {
            m_RecvSurplus.clear();
            return ;
        }

        char cmd = bytes[1]; //内部指令;

        //前五个字节中包含一条有意义的指令;
        if(bytes[4]=='.')
        {
            if(cmd == 'a') //交换机给此客户颁发的通信ID;
            {
                m_ClientID = bytes[2];
            //    qDebug() << "Client: " << m_ClientID << " connected!" << endl;
            }
            else if(cmd == '?') //此客户机因发送了非法消息而被交换机通告
            {
                this->InvaildMessage();
            }
            else if(cmd == 'm') //交换机因达到最大连接数而通知此客户已达最大连接数，此客户收到此数据后便会知晓连接被断开的原因;
            {
                this->MaxConnected();
            }
            else if(cmd == ']') //获得询问的目的客户是否可达的状态信息;
            {
                this->RspDestClientState(bytes[2],bytes[2]!='0');
            }
            else if(cmd == '}') //获得询问所有其它在线客户的客户列表之无其它客户在线时;
            {
                this->RspClientList(QByteArray());
            }

            //剩余的字节;
            bytes = bytes.right(bytes.length()-5);
        }
        else //说明是第二种数据结构的情况;
        {
            if(bytes.length() <= 9) //说明数据不全,需要下次接收时再一起拼接,跳出循环;
                break;

            uint data_len = GetUint(bytes.mid(4,4)); //数据长度;

            if(data_len==0 || data_len>1000)  //无数据或数据超长,残忍丢弃;
            {
              //  qDebug() << "data_len==0 or data_len>1000, droped!" << endl;
                m_RecvSurplus.clear();
                return ;
            }

            if((uint)bytes.length() < 9+data_len) //说明数据不全,需要下次接收时再一起拼接,跳出循环;
                break;

            if(bytes[8+data_len] != '.') //说明前9+data_len个字节不是一条有意义的指令，因为无结束符，无法分割，残忍丢弃;
            {
              //  qDebug() << "find not the terminator, droped!" << endl;
                m_RecvSurplus.clear();
                return ;
            }

            //前9+data_len个字节包含一条有意义的指令;
            if(cmd == '>') //单播数据;
            {
                this->RecvData(bytes[3],bytes.mid(8,data_len));
            }
            else if(cmd == '*') //广播数据;
            {
                this->RecvBroadcast(bytes[3],bytes.mid(8,data_len));
            }
            else if(cmd == 'x') //退件之目的不可达;
            {
                this->UnreachableMessage(bytes[2],bytes.mid(8,data_len));
            }
            else if(cmd == '}') //获得询问所有其它在线客户的客户列表之存在其它在线客户时;
            {
                this->RspClientList(bytes.mid(8,data_len));
            }

            //剩余的字节;
            bytes = bytes.right(bytes.length()-data_len-9);
        }
    }

    //先清空剩余字节(因为前面已经将它拼接);
    m_RecvSurplus.clear();

    //如果还剩下有未处理的字节，将之存放在m_RecvSurplus变量中;
    if(bytes.length()>0)
    {
        m_RecvSurplus.push_back(bytes);
    }
}

void MessageClient::GetErrorMsgSlot(QAbstractSocket::SocketError err)
{
  //  qDebug() << m_TcpSocket->errorString() << "," << err << endl;
}

void MessageClient::StateChangedSlot(QAbstractSocket::SocketState state)
{
    if(QAbstractSocket::ConnectedState == state)
    {
        //预约用户ID;
        if(m_ClientID != '\0')
        {
            QByteArray bytes;
            bytes.append("@a0");
            bytes.append(m_ClientID);
            bytes.append('.');
            this->SendMsg(bytes);
      //      qDebug() << m_ClientID << endl;
        }
    }
    else if(QAbstractSocket::UnconnectedState == state)
    {
      //  qDebug() << "disconnected..." << endl;
    }
}

void MessageClient::Start()
{
    if(m_TcpSocket && (m_TcpSocket->state()==QAbstractSocket::UnconnectedState))
    {
        m_RecvSurplus.clear();
        m_TcpSocket->connectToHost(m_Server,m_Port);
        bool ok = m_TcpSocket->waitForConnected();
     //   qDebug() << (ok?"connected ok!":"connected failed!") << endl;
    }
}

void MessageClient::Stop()
{
    m_TcpSocket->disconnectFromHost();
    bool ok = m_TcpSocket->waitForDisconnected();
  //  qDebug() << (ok?"disconnected ok!":"disconnected failed!") << endl;
}

char MessageClient::GetSelfClientID()
{
    return m_ClientID;
}

void MessageClient::Broadcast(QByteArray data)
{
    uint len = data.length();
    if( (m_ClientID !='\0') && (len>0 && len<1010) )
    {
        QByteArray bytes;
        bytes.append("@*0");
        bytes.append(m_ClientID);
        bytes.append(GetByteArray(len));
        bytes.append(data);
        bytes.append('.');
        this->SendMsg(bytes);
    }
}

void MessageClient::Unicast(char destClient,QByteArray data)
{
    uint len = data.length();
    if( (m_ClientID !='\0') && (len>0 && len<1010) &&
        ((destClient>='A'&&destClient<='Z')||(destClient>='a'&&destClient<='z')) )
    {
        QByteArray bytes;
        bytes.append("@>");
        bytes.append(destClient);
        bytes.append(m_ClientID);
        bytes.append(GetByteArray(len));
        bytes.append(data);
        bytes.append('.');
        this->SendMsg(bytes);
    }
}

void MessageClient::ReqDestClientState(char destClient)
{
    if( (m_ClientID !='\0') &&
        ((destClient>='A'&&destClient<='Z') || (destClient>='a'&&destClient<='z')) )
    {
        QByteArray bytes;
        bytes.append("@[");
        bytes.append(destClient);
        bytes.append(m_ClientID);
        bytes.append('.');
        this->SendMsg(bytes);
    }
}

void MessageClient::ReqClientList()
{
    if(m_ClientID !='\0')
    {
        QByteArray bytes;
        bytes.append("@{0");
        bytes.append(m_ClientID);
        bytes.append('.');
        this->SendMsg(bytes);
    }
}

void MessageClient::RecvData(char senderId,QByteArray data)
{
    emit RecvDataSignal(senderId,data);
}

void MessageClient::RecvBroadcast(char senderId, QByteArray data)
{
    emit RecvBroadcastSignal(senderId,data);
}

void MessageClient::UnreachableMessage(char destId,QByteArray data)
{
    emit UnreachableMessageSignal(destId,data);
}

void MessageClient::InvaildMessage()
{
    emit InvaildMessageSignal();
}

void MessageClient::MaxConnected()
{
    emit MaxConnectedSignal();
}

void MessageClient::RspDestClientState(char destId,bool online)
{
    emit RspDestClientStateSignal(destId,online);
}

void MessageClient::RspClientList(QByteArray destClients)
{
    emit RspClientListSignal(destClients);
}

void MessageClient::SendMsg(QByteArray bytes)
{
    uint size = m_TcpSocket->write(bytes);
    m_TcpSocket->flush();
    while(size<(uint)bytes.length())
    {
        size += m_TcpSocket->write(bytes.right(bytes.length()-size));
        m_TcpSocket->flush();
    }
}

uint MessageClient::GetUint(QByteArray bytes)
{
    uint sum = 0;
    if(bytes.length()==4)
    {
        sum = (bytes[0]-48)*1000 +
                (bytes[1]-48)*100 +
                (bytes[2]-48)*10 +
                (bytes[3]-48);
    }
    return sum;
}

QByteArray MessageClient::GetByteArray(uint num)
{
    QByteArray bytes;
    if(num>0 && num<1024)
    {
        if(num<10)
        {
            char n = num+48;
            bytes.append("000");
            bytes.append(n);
        }
        else if(num<100)
        {
            char n = num/10+48;
            bytes.append("00");
            bytes.append(n);
            n = num%10+48;
            bytes.append(n);
        }
        else if(num<1000)
        {
            char n = num/100+48;
            bytes.append('0');
            bytes.append(n);
            n = num%100/10+48;
            bytes.append(n);
            n = num%100%10+48;
            bytes.append(n);
        }
        else
        {
            char n = num/1000+48;
            bytes.append(n);
            n = num%1000/100+48;
            bytes.append(n);
            n = num%1000%100/10+48;
            bytes.append(n);
            n = num%1000%100%10+48;
            bytes.append(n);
        }
    }
    return bytes;
}
