#include "MessageCenter.h"

MessageCenter::MessageCenter(QObject *parent) : QObject(parent)
{
    m_TcpServer = new QTcpServer(this);
}

void MessageCenter::NewConnectionSlot()
{
    qDebug() << "new connection..." << endl;
    QTcpSocket* newSocket = m_TcpServer->nextPendingConnection();
    newSocket->setReadBufferSize(2000);
    newSocket->setSocketOption(QAbstractSocket::LowDelayOption,1);
    connect(newSocket,SIGNAL(readyRead()),this,SLOT(ReadyReadSlot()));
    connect(newSocket,SIGNAL(stateChanged(QAbstractSocket::SocketState)),this,SLOT(StateChangedSlot(QAbstractSocket::SocketState)));
}

void MessageCenter::ReadyReadSlot()
{
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if(!socket)
        return ;

    QByteArray bytes = socket->readAll();

    if(m_SurplusDataMap.keys().contains(int(socket)))
    {
        //未收到数据,直接返回;
        if(bytes.length()<=0)
            return ;

        QByteArray& surplusData = m_SurplusDataMap[int(socket)]; //注意是引用;

        //将上次剩余的数据拼接在最前面;
        if(surplusData.length()>0)
        {
            bytes.push_front(surplusData);
        }

        while(bytes.length()>=5) //有意义的数据包至少5个字节;
        {
            //收到的消息为非法消息，通告给当有客户此消息为非法消息;
            if(bytes[0]!='@') //首字符必须为'@'，否则被视为非法消息;
            {
                QByteArray bytes;
                bytes.append("@??0.");
                this->SendMsg(socket,bytes);
                surplusData.clear();
                return ;
            }

            char cmd = bytes[1]; //内部指令;

            //前五个字节中包含一条有意义的指令;
            if(bytes[4]=='.')
            {
                if(cmd == '[') //客户请求告知目的客户是否可达;
                {
                    bytes[1] = ']';
                    if(!m_ClientMap.keys().contains(bytes[2]))
                        bytes[2] = '0'; //被修改为'0'，说明不可达;
                    this->SendMsg(socket,bytes.left(5));    //回复是否可达;
                }
                else if(cmd == '{') //客户机请求的所有其它在线的客户的列表;
                {
                    bytes[1] = '}';
                    QByteArray idList = GetBytesOfOnlineClientId(bytes[3]);
                    if(idList.length() <= 0)
                    {
                        this->SendMsg(socket,bytes.left(5)); //回复无其它任何客户,此时数据包不作任何修改直接打回;
                    }
                    else
                    {
                        QByteArray tmp = bytes.left(4);
                        tmp.append(GetBytesOfDataLen(idList.length()));
                        tmp.append(idList);
                        tmp.append('.');
                        this->SendMsg(socket,tmp); //回复其它在线的客户列表;
                    }
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
            //        qDebug() << "data_len==0 or data_len>1000, droped!" << endl;
                    surplusData.clear();
                    return ;
                }

                if((uint)bytes.length() < 9+data_len) //说明数据不全,需要下次接收时再一起拼接,跳出循环;
                    break;

                if(bytes[8+data_len] != '.') //说明前9+data_len个字节不是一条有意义的指令，因为无结束符，无法分割，残忍丢弃;
                {
             //       qDebug() << "find not the terminator, droped!" << endl;
                    surplusData.clear();
                    return ;
                }

                //前9+data_len个字节包含一条有意义的指令;
                if(cmd == '>') //单播数据;
                {
                    if(m_ClientMap.keys().contains(bytes[2])) //接收者在线，转发;
                    {
                        this->SendMsg(m_ClientMap[bytes[2]],bytes.left(9+data_len));
                    }
                    else //目标不可达,修改内部指令，然后退件;
                    {
                        bytes[1]='x';
                        this->SendMsg(socket,bytes.left(9+data_len)); //回复数据不可达;
                    }
                }
                else if(cmd == '*') //广播数据;
                {
                    this->Broadcast(bytes[3],bytes.left(9+data_len)); //将广播类消息广播出去;
                }

                //剩余的字节;
                bytes = bytes.right(bytes.length()-data_len-9);
            }
        }

        //先清空剩余字节(因为前面已经将它拼接);
        surplusData.clear();

        //如果还剩下有未处理的字节，将之存放在surplusData引用变量中;
        if(bytes.length()>0)
        {
            surplusData.push_back(bytes);
        }
    }
    else //第一次连接;
    {
        if(bytes.length()==5 && bytes[4]=='.' && bytes.left(3)=="@a0")
            this->FirstConn(bytes[3],socket);
    }

}

void MessageCenter::StateChangedSlot(QAbstractSocket::SocketState state)
{
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if(socket && (QAbstractSocket::ConnectedState == state))
    {
    }
    else if( socket && (QAbstractSocket::UnconnectedState == state))
    {
        QMap<char,QTcpSocket*>::const_iterator it = m_ClientMap.begin();
        for(;it!=m_ClientMap.end();it++)
        {
            if(it.value() == socket)
            {
                qDebug() << "Server: " << it.key() << " removed!" << endl;
                m_ClientMap.remove(it.key());
                m_SurplusDataMap.remove(int(socket));
                break;
            }
        }
    }
}

void MessageCenter::Start()
{
    if(m_TcpServer->listen(QHostAddress::AnyIPv4,9981))
    {
        connect(m_TcpServer,SIGNAL(newConnection()),this,SLOT(NewConnectionSlot()));
    }
}

void MessageCenter::FirstConn(char senderId,QTcpSocket* newSocket)
{
    char id = this->DistributeId(senderId,newSocket->peerAddress().toString()); //如果是内部模块，则为127.0.0.1
    if(id=='\0')//已达最大连接数，通知后断开其socket连接;
    {
        QByteArray bytes;
        bytes.append("@m?0.");
        this->SendMsg(newSocket,bytes);
        newSocket->close(); //断开此socket连接;
        return ;
    }
    else //颁发给此客户一个通信ID;
    {
        QByteArray bytes;
        bytes.append("@a");
        bytes.append(id);
        bytes.append("0.");
        this->SendMsg(newSocket,bytes);\
        qDebug() << "server: " << id << " connected!" << endl;
        m_ClientMap.insert(id,newSocket);
        m_SurplusDataMap.insert(int(newSocket),QByteArray());
    }
}

void MessageCenter::Broadcast(char senderId,QByteArray bytes)
{
    QMap<char,QTcpSocket*>::const_iterator it = m_ClientMap.begin();
    for(;it!=m_ClientMap.end();it++)
    {
        if(it.key() != senderId)
        {
            this->SendMsg(it.value(),bytes);
        }
    }
}

void MessageCenter::SendMsg(QTcpSocket* socket, QByteArray bytes)
{
    uint size = socket->write(bytes);
    socket->flush();
    while(size<(uint)bytes.length())
    {
        size += socket->write(bytes.right(bytes.length()-size));
        socket->flush();
    }
}

char MessageCenter::DistributeId(char senderId,const QString& address)
{
    QList<char> ids = m_ClientMap.keys();
    if( (senderId>='A'&&senderId<='Z') || (senderId>='a'&&senderId<='z'))
    {
        if(!ids.contains(senderId))//如果客户ID的列表中没有，则返回此预约的ID;
            return senderId;
    }
    if(address=="127.0.0.1") //内部组件;
    {
        for(char i='A';i<='Z';i++)
        {
            if(!ids.contains(i))
            {
                return i;
            }
        }
    }
    else //外部组件;
    {
        for(char i='a';i<='z';i++)
        {
            if(!ids.contains(i))
            {
                return i;
            }
        }
    }

    return '\0';
}

uint MessageCenter::GetUint(QByteArray bytes)
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

QByteArray MessageCenter::GetBytesOfDataLen(uint num)
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

QByteArray MessageCenter::GetBytesOfOnlineClientId(char senderId)
{
    QByteArray bytes;
    bytes.clear();
    if(m_ClientMap.count()>0 && m_ClientMap.keys().contains(senderId))
    {
        QMap<char,QTcpSocket*>::const_iterator it = m_ClientMap.begin();
        for(;it!=m_ClientMap.end();it++)
        {
            char id = it.key();
            if(id != senderId)
            {
                bytes.append(id);
            }
        }
    }
    return bytes;
}
