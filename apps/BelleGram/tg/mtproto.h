// BelleGram MTProto 2.0 baglantisi (tek veri merkezi): TCP "intermediate" tasima, yetki anahtari uretimi
// (req_pq_multi -> RSA_PAD -> Diffie-Hellman), sifreli mesajlar, hizmet iletileri (rpc_result, msg_container,
// gzip_packed, bad_server_salt, bad_msg_notification, new_session_created, msgs_ack), canli tutma.
// TLS kullanilmaz: MTProto kendi sifrelemesini yapar.
#ifndef TG_MTPROTO_H
#define TG_MTPROTO_H

#include <QtCore/QObject>
#include <QtCore/QHash>
#include <QtCore/QList>
#include <QtCore/QTimer>
#include <QtCore/QVariant>
#include <QtNetwork/QTcpSocket>

class Baglanti : public QObject
{
    Q_OBJECT
public:
    struct InitBilgi {
        int apiId;
        QString cihaz, sistem, surum, dil;
    };

    Baglanti(int dc, const QString &ip, int port, bool test, QObject *parent = 0);
    ~Baglanti();

    int dc() const { return m_dc; }
    void initAyarla(const InitBilgi &b) { m_init = b; }
    void anahtarAyarla(const QByteArray &yetkiAnahtari, qint64 tuz);
    QByteArray anahtar() const { return m_anahtar; }
    qint64 tuz() const { return m_tuz; }
    bool hazir() const { return m_hazir; }
    int saatFarki() const { return m_saatFarki; }

    void baglan();
    void kapat();
    // Govde: serilestirilmis TL fonksiyonu. sonucTuru: yanitin okunacagi tur. Donus: istek numarasi
    // tekrar=false: baglanti koparsa yeniden GONDERILMEZ, hata(istek, -1, "BAGLANTI_KOPTU") verilir
    // (auth.sendCode her gonderimde yeni kod uretir; tekrar gonderilince elde edilen kod gecersiz kalir)
    int gonder(const QByteArray &govde, const QByteArray &sonucTuru, bool tekrar = true);

signals:
    void anahtarHazir();                       // yetki anahtari var ve baglanti kuruldu
    void sonuc(int istek, const QVariant &v);
    void hata(int istek, int kod, const QString &mesaj);
    void guncelleme(const QVariant &v);        // sunucunun kendiliginden gonderdigi Updates
    void koptu(const QString &neden);
    void gunluk(const QString &s);

private slots:
    void baglandi();
    void okunabilir();
    void soketHatasi(QAbstractSocket::SocketError e);
    void pingAt();

private:
    struct Bekleyen {
        Bekleyen() : istek(0), icerik(true), sarili(false), tekrar(true) {}
        int istek; QByteArray govde; QByteArray ham; QByteArray tur; bool icerik; bool sarili;
        bool tekrar;   // baglanti koparsa yeniden gonderilsin mi (giris istekleri: hayir)
    };

    void cerceveYaz(const QByteArray &paket);
    void paketIsle(const QByteArray &paket);
    // yetki anahtari
    void elSikismaBasla();
    void duzGonder(const QByteArray &govde);
    void elSikismaIsle(const QByteArray &govde);
    void istemciDhGonder();
    void anahtarTamam();
    // sifreli
    qint64 yeniMsgId();
    qint64 sifreliGonder(const QByteArray &govde, bool icerik);
    void kdf(const QByteArray &msgKey, int x, QByteArray &key, QByteArray &iv) const;
    void sifreliIsle(const QByteArray &paket);
    void isle(const QByteArray &govde, qint64 msgId);
    void yenidenGonder(qint64 eskiMsgId);
    void onayGonder();
    void kuyruguBosalt();
    QByteArray sarmala(const QByteArray &govde);

    int m_dc, m_port;
    QString m_ip;
    bool m_test;
    QTcpSocket *m_soket;
    QByteArray m_gelen;
    InitBilgi m_init;
    bool m_hazir, m_initTamam;
    // el sikisma
    int m_adim;
    QByteArray m_nonce, m_sunucuNonce, m_yeniNonce, m_tmpKey, m_tmpIv, m_dhAsal, m_gA, m_b;
    int m_g;
    qint64 m_yenidenId;
    // oturum
    QByteArray m_anahtar, m_anahtarId, m_oturum;
    qint64 m_tuz, m_sonMsgId;
    int m_seq, m_saatFarki;
    QHash<qint64, Bekleyen> m_bekleyen;
    QList<Bekleyen> m_kuyruk;
    QList<qint64> m_onay;
    int m_sonIstek;
    QTimer m_ping;
};

#endif
