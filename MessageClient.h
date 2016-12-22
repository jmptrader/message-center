#ifndef MESSAGECLIENT_H
#define MESSAGECLIENT_H

#include <QObject>
#include <QTcpSocket>

class MessageClient : public QObject
{
    Q_OBJECT
public:
    MessageClient(char id,uint port,const QString& server,QObject* parent=0);//id为客户预约的Id,真实拿到的ID不一定是预约的ID,这个得注意了;
    ~MessageClient();
signals://发送给其它的对象，比如UI;
    void    RecvDataSignal(char senderId,QByteArray data);
    void    RecvBroadcastSignal(char senderId,QByteArray data);
    void    UnreachableMessageSignal(char destId,QByteArray data);
    void    InvaildMessageSignal();
    void    MaxConnectedSignal();
    void    RspDestClientStateSignal(char destId,bool online);
    void    RspClientListSignal(QByteArray data);

public slots: //因为防止界面的循环造成socket的发送与接收阻塞，应单独放入一线程，并通过信号来与这些槽函数相连，以达到控制的目的;
    void    StartSlot();
    void    StopSlot();
    void    BroadcastSlot(QByteArray data);
    void    UnicastSlot(char destClient,QByteArray data);
    void    ReqDestClientStateSlot(char destClient);
    void    ReqClientListSlot();
    void    CloseClientSlot();
private slots:
    void    ReadyReadSlot();
    void    GetErrorMsgSlot(QAbstractSocket::SocketError err);
    void    StateChangedSlot(QAbstractSocket::SocketState state);
public:
    void    Start(); //启动连接;
    void    Stop(); //断开与服务器的连接;
    char    GetSelfClientID(); //获取自身的客户ID;
public:
    void    Broadcast(QByteArray data); //广播(广播发送);
    void    Unicast(char destClient,QByteArray data); //单播(定向发送);
    void    ReqDestClientState(char destClient); //请求目的客户在线状态;
    void    ReqClientList(); //请求在线客户列表;
public://virtual;可以重载;
    virtual void RecvData(char senderId,QByteArray data); //获取到的普通数据;
    virtual void RecvBroadcast(char senderId,QByteArray data); //获取到广播数据;
    virtual void UnreachableMessage(char destId,QByteArray data); //获取到不可达的退件数据;
    virtual void InvaildMessage();//获取到的从交换机定义的非法消息的回复;
    virtual void MaxConnected(); //交换机已达最大连接数时的回复;
    virtual void RspDestClientState(char destId,bool online); //响应目的客户在线状态的请求;
    virtual void RspClientList(QByteArray destClients); //响应在线客户列表的请求;

private:
    void        SendMsg(QByteArray bytes); //发送消息;
    uint        GetUint(QByteArray bytes); //根据字节内容转换成数据长度;
    QByteArray  GetByteArray(uint num); //根据数据长度转换成字节内容;
private:
    uint        m_Port; //端口号;
    QString     m_Server; //服务器地址;
    char        m_ClientID; //本客户ID;
    QByteArray  m_RecvSurplus; //剩余数据段;
private:
    QTcpSocket* m_TcpSocket;
};

#endif // MESSAGECLIENT_H
