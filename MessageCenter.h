#ifndef MESSAGECENTER_H
#define MESSAGECENTER_H


#include <QObject>
#include <QTcpSocket>
#include <QTcpServer>

class MessageCenter : public QObject
{
    Q_OBJECT
public:
    MessageCenter(QObject *parent=0);
private slots:
    void    NewConnectionSlot();
    void    ReadyReadSlot();
    void    StateChangedSlot(QAbstractSocket::SocketState state);
public:
    void    Start();
private:
    void        FirstConn(char senderId,QTcpSocket* newSocket);
    void        Broadcast(char senderId,QByteArray bytes);
    void        SendMsg(QTcpSocket* socket, QByteArray bytes);
    char        DistributeId(char senderId,const QString& address);
    uint        GetUint(QByteArray bytes);
    QByteArray  GetBytesOfDataLen(uint num);
    QByteArray  GetBytesOfOnlineClientId(char senderId);
private:    //配置参数;
    uint       m_MaxMsgLen;
private:
    QTcpServer*             m_TcpServer;
    QMap<char,QTcpSocket*>  m_ClientMap;
    QMap<int,QByteArray>    m_SurplusDataMap;
};

#endif // MESSAGECENTER_H
