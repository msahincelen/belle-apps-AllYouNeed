#include "mtproto.h"
#include "tl.h"
#include "sifre.h"
#include "buyuksayi.h"
#include <QtCore/QDateTime>
#include <QtCore/QStringList>
#include <string.h>

// ----------------------------------------------------------------------- sabitler
static const quint32 KReqPqMulti = 0xbe7e8ef1;
static const quint32 KResPQ = 0x05162463;
static const quint32 KServerDhOk = 0xd0e8075c;
static const quint32 KServerDhFail = 0x79cb045d;
static const quint32 KDhGenOk = 0x3bcbf734;
static const quint32 KDhGenRetry = 0x46dc1fb9;
static const quint32 KDhGenFail = 0xa69dae02;
static const quint32 KRpcResult = 0xf35c6d01;
static const quint32 KRpcError = 0x2144ca19;
static const quint32 KContainer = 0x73f1f8dc;
static const quint32 KGzip = 0x3072cfa1;
static const quint32 KBadSalt = 0xedab447b;
static const quint32 KBadMsg = 0xa7eff811;
static const quint32 KNewSession = 0x9ec20908;
static const quint32 KMsgsAck = 0x62d6b459;
static const quint32 KPong = 0x347773c5;
static const quint32 KDetayli = 0x276d3ec6;
static const quint32 KYeniDetayli = 0x809db6df;
static const quint32 KPingDelay = 0xf3427b8c;

// Telegram'in uretim (canli) sunucu anahtari: my.telegram.org "Canli ortam yapilandirmasi" (herkese acik anahtar)
static const char *KUretimPem =
    "MIIBCgKCAQEA6LszBcC1LGzyr992NzE0ieY+BSaOW622Aa9Bd4ZHLl+TuFQ4lo4g"
    "5nKaMBwK/BIb9xUfg0Q29/2mgIR6Zr9krM7HjuIcCzFvDtr+L0GQjae9H0pRB2OO"
    "62cECs5HKhT5DZ98K33vmWiLowc621dQuwKWSQKjWf50XYFw42h21P2KXUGyp2y/"
    "+aEyZ+uVgLLQbRA1dEjSDZ2iGRy12Mk5gpYc397aYp438fsJoHIgJ2lgMv5h7WY9"
    "t6N/byY9Nw9p21Og3AoXSL2q/2IJ1WRUhebgAdGVMlV1fkuOQoEzR7EdpqtQD9Cs"
    "5+bfo3Nhmcyvk5ftB0WkJ9z6bNZ7yxrP8wIDAQAB";
// Test sunuculari (149.154.167.40)
static const char *KTestPem =
    "MIIBCgKCAQEAyMEdY1aR+sCR3ZSJrtztKTKqigvO/vBfqACJLZtS7QMgCGXJ6XIR"
    "yy7mx66W0/sOFa7/1mAZtEoIokDP3ShoqF4fVNb6XeqgQfaUHd8wJpDWHcR2OFwv"
    "plUUI1PLTktZ9uW2WE23b+ixNwJjJGwBDJPQEQFBE+vfmH0JP503wr5INS1poWg/"
    "j25sIWeYPHYeOrFp/eXaqhISP6G+q2IeTaWTXpwZj4LzXq5YOpk4bYEQ6mvRq7D1"
    "aHWfYmlEGepfaYR8Q0YqvvhYtMte3ITnuSJs171+GDqpdKcSwHnd6FudwGO4pcCO"
    "j4WcDuXc2CTHgH8gFTNhp/Y8/SpDOhvn9QIDAQAB";

struct RsaAnahtar { QByteArray n, e; qint64 parmakIzi; };

static int derUzunluk(const QByteArray &d, int &i)
{
    int n = uchar(d.at(i++));
    if (n & 0x80) {
        int k = n & 0x7f; n = 0;
        while (k--) n = (n << 8) | uchar(d.at(i++));
    }
    return n;
}

static RsaAnahtar rsaCoz(const char *pem)
{
    RsaAnahtar a;
    const QByteArray d = QByteArray::fromBase64(QByteArray(pem));
    int i = 0;
    if (d.size() < 10 || uchar(d.at(i++)) != 0x30) return a;
    derUzunluk(d, i);
    if (uchar(d.at(i++)) != 0x02) return a;
    int n = derUzunluk(d, i);
    QByteArray mod = d.mid(i, n); i += n;
    while (mod.size() > 1 && mod.at(0) == 0) mod.remove(0, 1);
    if (uchar(d.at(i++)) != 0x02) return a;
    n = derUzunluk(d, i);
    a.n = mod;
    a.e = d.mid(i, n);
    TlYazici w;
    w.bayt(a.n);
    w.bayt(a.e);
    const QByteArray h = Sifre::sha1(w.veri).right(8);
    memcpy(&a.parmakIzi, h.constData(), 8);
    return a;
}

static QByteArray xorla(const QByteArray &a, const QByteArray &b)
{
    QByteArray r(a.size(), 0);
    for (int i = 0; i < a.size(); ++i) r[i] = char(a.at(i) ^ b.at(i % b.size()));
    return r;
}

static QByteArray sayiBayt(quint64 v)
{
    QByteArray b;
    while (v) { b.prepend(char(v & 0xff)); v >>= 8; }
    return b;
}

static qint64 i64(const QByteArray &b, int ofs) { qint64 v = 0; memcpy(&v, b.constData() + ofs, 8); return v; }
static quint32 u32(const QByteArray &b, int ofs) { quint32 v = 0; memcpy(&v, b.constData() + ofs, 4); return v; }

// ======================================================================= yasam dongusu
Baglanti::Baglanti(int dc, const QString &ip, int port, bool test, QObject *parent)
    : QObject(parent), m_dc(dc), m_port(port), m_ip(ip), m_test(test), m_soket(0), m_hazir(false), m_initTamam(false),
      m_adim(0), m_g(0), m_yenidenId(0), m_tuz(0), m_sonMsgId(0), m_seq(0), m_saatFarki(0), m_sonIstek(0)
{
    m_init.apiId = 0;
    m_ping.setInterval(55000);
    connect(&m_ping, SIGNAL(timeout()), this, SLOT(pingAt()));
}

Baglanti::~Baglanti()
{
    kapat();
}

void Baglanti::anahtarAyarla(const QByteArray &a, qint64 tuz)
{
    if (a.size() != 256) return;
    m_anahtar = a;
    m_anahtarId = Sifre::sha1(a).right(8);
    m_tuz = tuz;
}

void Baglanti::baglan()
{
    if (m_soket) return;
    m_soket = new QTcpSocket(this);
    m_soket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    connect(m_soket, SIGNAL(connected()), this, SLOT(baglandi()));
    connect(m_soket, SIGNAL(readyRead()), this, SLOT(okunabilir()));
    connect(m_soket, SIGNAL(error(QAbstractSocket::SocketError)), this, SLOT(soketHatasi(QAbstractSocket::SocketError)));
    m_gelen.clear();
    emit gunluk(QString::fromLatin1("DC%1 %2:%3 baglaniliyor").arg(m_dc).arg(m_ip).arg(m_port));
    m_soket->connectToHost(m_ip, quint16(m_port));
}

void Baglanti::kapat()
{
    m_ping.stop();
    m_hazir = false;
    if (m_soket) {
        m_soket->disconnect(this);
        m_soket->abort();
        m_soket->deleteLater();
        m_soket = 0;
    }
}

void Baglanti::baglandi()
{
    // intermediate tasima: ilk 4 bayt 0xeeeeeeee
    m_soket->write(QByteArray(4, char(0xee)));
    m_initTamam = false;
    if (m_anahtar.isEmpty()) { elSikismaBasla(); return; }
    if (m_oturum.isEmpty()) { m_oturum = Sifre::rastgele(8); m_seq = 0; }
    m_hazir = true;
    m_ping.start();
    emit gunluk(QString::fromLatin1("DC%1 hazir (kayitli anahtar)").arg(m_dc));
    emit anahtarHazir();
    // baglanti koptuysa yanit bekleyenleri yeniden gonder; tekrarlanmamasi gerekenler (giris) hata olarak doner
    const QList<qint64> eski = m_bekleyen.keys();
    for (int i = 0; i < eski.size(); ++i) {
        if (m_bekleyen.value(eski[i]).tekrar) { yenidenGonder(eski[i]); continue; }
        const Bekleyen b = m_bekleyen.take(eski[i]);
        emit gunluk(QString::fromLatin1("DC%1 istek %2 baglanti koptugu icin tekrarlanmadi").arg(m_dc).arg(b.istek));
        emit hata(b.istek, -1, QString::fromLatin1("BAGLANTI_KOPTU"));
    }
    kuyruguBosalt();
}

void Baglanti::soketHatasi(QAbstractSocket::SocketError)
{
    const QString neden = m_soket ? m_soket->errorString() : QString();
    emit gunluk(QString::fromLatin1("DC%1 soket hatasi: %2").arg(m_dc).arg(neden));
    kapat();
    emit koptu(neden);
}

void Baglanti::cerceveYaz(const QByteArray &paket)
{
    if (!m_soket) return;
    QByteArray f;
    const qint32 n = paket.size();
    f.append(reinterpret_cast<const char *>(&n), 4);
    f.append(paket);
    m_soket->write(f);
}

void Baglanti::okunabilir()
{
    m_gelen.append(m_soket->readAll());
    while (m_gelen.size() >= 4) {
        const qint32 n = qint32(u32(m_gelen, 0));
        if (n < 0 || n > 16 * 1024 * 1024) { emit gunluk(QString::fromLatin1("bozuk cerceve")); kapat(); emit koptu(QString::fromLatin1("bozuk cerceve")); return; }
        if (m_gelen.size() < 4 + n) break;
        const QByteArray paket = m_gelen.mid(4, n);
        m_gelen.remove(0, 4 + n);
        paketIsle(paket);
        if (!m_soket) return;
    }
}

void Baglanti::paketIsle(const QByteArray &p)
{
    if (p.size() == 4) {
        const qint32 kod = qint32(u32(p, 0));
        emit gunluk(QString::fromLatin1("DC%1 tasima hatasi %2").arg(m_dc).arg(kod));
        if (kod == -404) {
            // anahtar sunucuda yok: yeni anahtar uret
            m_anahtar.clear(); m_anahtarId.clear(); m_oturum.clear();
            m_hazir = false;
            elSikismaBasla();
        }
        return;
    }
    if (p.size() < 8) return;
    if (i64(p, 0) == 0) {
        // sifresiz (el sikisma): auth_key_id(8) msg_id(8) uzunluk(4) govde
        if (p.size() < 20) return;
        elSikismaIsle(p.mid(20, qint32(u32(p, 16))));
    } else {
        sifreliIsle(p);
    }
}

// ======================================================================= yetki anahtari
void Baglanti::duzGonder(const QByteArray &govde)
{
    QByteArray p(8, 0);
    const qint64 id = yeniMsgId();
    p.append(reinterpret_cast<const char *>(&id), 8);
    const qint32 n = govde.size();
    p.append(reinterpret_cast<const char *>(&n), 4);
    p.append(govde);
    cerceveYaz(p);
}

void Baglanti::elSikismaBasla()
{
    emit gunluk(QString::fromLatin1("DC%1 yetki anahtari uretiliyor").arg(m_dc));
    m_nonce = Sifre::rastgele(16);
    m_adim = 1;
    TlYazici w;
    w.uint32(KReqPqMulti);
    w.ham(m_nonce);
    duzGonder(w.veri);
}

void Baglanti::elSikismaIsle(const QByteArray &govde)
{
    TlOkuyucu r(govde);
    const quint32 id = r.uint32();
    if (m_adim == 1 && id == KResPQ) {
        if (r.ham(16) != m_nonce) { emit gunluk(QString::fromLatin1("resPQ: nonce uyusmadi")); return; }
        m_sunucuNonce = r.ham(16);
        const QByteArray pqB = r.bayt();
        r.uint32();   // Vector
        const int nfp = r.int32();
        QList<qint64> izler;
        for (int i = 0; i < nfp; ++i) izler.append(r.int64());
        const RsaAnahtar ak = rsaCoz(m_test ? KTestPem : KUretimPem);
        if (!izler.contains(ak.parmakIzi)) { emit gunluk(QString::fromLatin1("resPQ: sunucu anahtari tanidik degil")); return; }
        quint64 pq = 0;
        for (int i = 0; i < pqB.size(); ++i) pq = (pq << 8) | uchar(pqB.at(i));
        quint64 p = 0, q = 0;
        if (!Sifre::carpanla(pq, p, q)) { emit gunluk(QString::fromLatin1("pq carpanlanamadi")); return; }
        m_yeniNonce = Sifre::rastgele(32);
        // p_q_inner_data_dc
        TlYazici ic;
        ic.uint32(0xa9f55f95);
        ic.bayt(pqB);
        ic.bayt(sayiBayt(p));
        ic.bayt(sayiBayt(q));
        ic.ham(m_nonce);
        ic.ham(m_sunucuNonce);
        ic.ham(m_yeniNonce);
        ic.int32(m_test ? 10000 + m_dc : m_dc);
        // RSA_PAD
        QByteArray veri = ic.veri;
        if (veri.size() > 144) { emit gunluk(QString::fromLatin1("ic veri cok uzun")); return; }
        const QByteArray dolgulu = veri + Sifre::rastgele(192 - veri.size());
        QByteArray ters;
        for (int i = dolgulu.size() - 1; i >= 0; --i) ters.append(dolgulu.at(i));
        const BuyukSayi n = BuyukSayi::baytlardan(ak.n), e = BuyukSayi::baytlardan(ak.e);
        QByteArray sifreli;
        for (int deneme = 0; deneme < 20; ++deneme) {
            const QByteArray tmp = Sifre::rastgele(32);
            const QByteArray ozetli = ters + Sifre::sha256(tmp + dolgulu);
            const QByteArray aesli = Sifre::igeSifrele(ozetli, tmp, QByteArray(32, 0));
            const QByteArray anahtarXor = xorla(tmp, Sifre::sha256(aesli));
            const QByteArray tum = anahtarXor + aesli;   // 256 bayt
            const BuyukSayi x = BuyukSayi::baytlardan(tum);
            if (x >= n) continue;
            sifreli = BuyukSayi::usMod(x, e, n).baytlar(256);
            break;
        }
        if (sifreli.isEmpty()) { emit gunluk(QString::fromLatin1("RSA_PAD basarisiz")); return; }
        TlYazici w;
        w.uint32(0xd712e4be);   // req_DH_params
        w.ham(m_nonce);
        w.ham(m_sunucuNonce);
        w.bayt(sayiBayt(p));
        w.bayt(sayiBayt(q));
        w.int64(ak.parmakIzi);
        w.bayt(sifreli);
        m_adim = 2;
        duzGonder(w.veri);
        return;
    }
    if (m_adim == 2 && (id == KServerDhOk || id == KServerDhFail)) {
        if (r.ham(16) != m_nonce || r.ham(16) != m_sunucuNonce) { emit gunluk(QString::fromLatin1("DH: nonce uyusmadi")); return; }
        if (id == KServerDhFail) { emit gunluk(QString::fromLatin1("server_DH_params_fail")); elSikismaBasla(); return; }
        const QByteArray sifreliCevap = r.bayt();
        const QByteArray h1 = Sifre::sha1(m_yeniNonce + m_sunucuNonce);
        const QByteArray h2 = Sifre::sha1(m_sunucuNonce + m_yeniNonce);
        const QByteArray h3 = Sifre::sha1(m_yeniNonce + m_yeniNonce);
        m_tmpKey = h1 + h2.left(12);
        m_tmpIv = h2.mid(12, 8) + h3 + m_yeniNonce.left(4);
        const QByteArray cevap = Sifre::igeCoz(sifreliCevap, m_tmpKey, m_tmpIv);
        TlOkuyucu c(cevap.mid(20));
        if (c.uint32() != 0xb5890dba) { emit gunluk(QString::fromLatin1("server_DH_inner_data bekleniyordu")); return; }
        if (c.ham(16) != m_nonce || c.ham(16) != m_sunucuNonce) { emit gunluk(QString::fromLatin1("DH ic: nonce uyusmadi")); return; }
        m_g = c.int32();
        m_dhAsal = c.bayt();
        m_gA = c.bayt();
        const qint32 sunucuSaat = c.int32();
        if (c.hata || Sifre::sha1(cevap.mid(20, c.konum)) != cevap.left(20)) { emit gunluk(QString::fromLatin1("DH ic: ozet uyusmadi")); return; }
        m_saatFarki = sunucuSaat - int(QDateTime::currentDateTime().toTime_t());
        if (m_g < 2 || m_g > 7 || m_dhAsal.size() != 256) { emit gunluk(QString::fromLatin1("DH parametreleri gecersiz")); return; }
        m_yenidenId = 0;
        istemciDhGonder();
        return;
    }
    if (m_adim == 3 && (id == KDhGenOk || id == KDhGenRetry || id == KDhGenFail)) {
        if (r.ham(16) != m_nonce || r.ham(16) != m_sunucuNonce) { emit gunluk(QString::fromLatin1("dh_gen: nonce uyusmadi")); return; }
        const QByteArray yeniNonceOzet = r.ham(16);
        const BuyukSayi asal = BuyukSayi::baytlardan(m_dhAsal);
        const QByteArray anahtar = BuyukSayi::usMod(BuyukSayi::baytlardan(m_gA), BuyukSayi::baytlardan(m_b), asal).baytlar(256);
        const QByteArray yardimci = Sifre::sha1(anahtar).left(8);
        const char no = id == KDhGenOk ? 1 : id == KDhGenRetry ? 2 : 3;
        const QByteArray beklenen = Sifre::sha1(m_yeniNonce + QByteArray(1, no) + yardimci).mid(4, 16);
        if (beklenen != yeniNonceOzet) { emit gunluk(QString::fromLatin1("dh_gen: new_nonce_hash uyusmadi")); return; }
        if (id == KDhGenRetry) { memcpy(&m_yenidenId, yardimci.constData(), 8); istemciDhGonder(); return; }
        if (id == KDhGenFail) { emit gunluk(QString::fromLatin1("dh_gen_fail")); elSikismaBasla(); return; }
        m_anahtar = anahtar;
        m_anahtarId = Sifre::sha1(anahtar).right(8);
        m_tuz = i64(xorla(m_yeniNonce.left(8), m_sunucuNonce.left(8)), 0);
        anahtarTamam();
        return;
    }
    emit gunluk(QString::fromLatin1("beklenmeyen el sikisma yaniti 0x%1 (adim %2)").arg(id, 8, 16, QLatin1Char('0')).arg(m_adim));
}

void Baglanti::istemciDhGonder()
{
    m_b = Sifre::rastgele(256);
    const BuyukSayi asal = BuyukSayi::baytlardan(m_dhAsal);
    const QByteArray gB = BuyukSayi::usMod(BuyukSayi(quint32(m_g)), BuyukSayi::baytlardan(m_b), asal).baytlar(256);
    TlYazici ic;
    ic.uint32(0x6643b654);   // client_DH_inner_data
    ic.ham(m_nonce);
    ic.ham(m_sunucuNonce);
    ic.int64(m_yenidenId);
    ic.bayt(gB);
    QByteArray veri = Sifre::sha1(ic.veri) + ic.veri;
    if (veri.size() % 16) veri.append(Sifre::rastgele(16 - veri.size() % 16));
    TlYazici w;
    w.uint32(0xf5045f1f);   // set_client_DH_params
    w.ham(m_nonce);
    w.ham(m_sunucuNonce);
    w.bayt(Sifre::igeSifrele(veri, m_tmpKey, m_tmpIv));
    m_adim = 3;
    duzGonder(w.veri);
}

void Baglanti::anahtarTamam()
{
    m_adim = 0;
    m_oturum = Sifre::rastgele(8);
    m_seq = 0;
    m_hazir = true;
    m_ping.start();
    emit gunluk(QString::fromLatin1("DC%1 yetki anahtari hazir").arg(m_dc));
    emit anahtarHazir();
    kuyruguBosalt();
}

// ======================================================================= sifreli mesajlar
qint64 Baglanti::yeniMsgId()
{
    const qint64 ms = QDateTime::currentDateTime().toMSecsSinceEpoch() + qint64(m_saatFarki) * 1000;
    const qint64 sn = ms / 1000;
    const quint32 kesir = quint32((ms % 1000) * 4294967LL);
    qint64 id = (sn << 32) | qint64(kesir & ~3u);
    if (id <= m_sonMsgId) id = m_sonMsgId + 4;
    m_sonMsgId = id;
    return id;
}

void Baglanti::kdf(const QByteArray &msgKey, int x, QByteArray &key, QByteArray &iv) const
{
    const QByteArray a = Sifre::sha256(msgKey + m_anahtar.mid(x, 36));
    const QByteArray b = Sifre::sha256(m_anahtar.mid(40 + x, 36) + msgKey);
    key = a.left(8) + b.mid(8, 16) + a.mid(24, 8);
    iv = b.left(8) + a.mid(8, 16) + b.mid(24, 8);
}

qint64 Baglanti::sifreliGonder(const QByteArray &govde, bool icerik)
{
    const qint64 msgId = yeniMsgId();
    const qint32 seq = icerik ? (m_seq++ * 2 + 1) : (m_seq * 2);
    QByteArray d;
    d.append(reinterpret_cast<const char *>(&m_tuz), 8);
    d.append(m_oturum);
    d.append(reinterpret_cast<const char *>(&msgId), 8);
    d.append(reinterpret_cast<const char *>(&seq), 4);
    const qint32 n = govde.size();
    d.append(reinterpret_cast<const char *>(&n), 4);
    d.append(govde);
    int dolgu = 16 - (d.size() % 16);
    if (dolgu < 12) dolgu += 16;
    d.append(Sifre::rastgele(dolgu));
    const QByteArray msgKey = Sifre::sha256(m_anahtar.mid(88, 32) + d).mid(8, 16);
    QByteArray key, iv;
    kdf(msgKey, 0, key, iv);
    cerceveYaz(m_anahtarId + msgKey + Sifre::igeSifrele(d, key, iv));
    return msgId;
}

QByteArray Baglanti::sarmala(const QByteArray &govde)
{
    // invokeWithLayer(katman, initConnection(..., query))
    TlYazici w;
    w.uint32(0xda9b0d0d);
    w.int32(TlSema::tek().katman());
    w.uint32(0xc1cd5ea9);
    w.uint32(0);   // flags
    w.int32(m_init.apiId);
    w.dize(m_init.cihaz);
    w.dize(m_init.sistem);
    w.dize(m_init.surum);
    w.dize(m_init.dil);
    w.dize(QString());       // lang_pack
    w.dize(m_init.dil);
    w.ham(govde);
    return w.veri;
}

int Baglanti::gonder(const QByteArray &govde, const QByteArray &sonucTuru, bool tekrar)
{
    Bekleyen b;
    b.istek = ++m_sonIstek;
    b.govde = govde;
    b.ham = govde;
    b.tur = sonucTuru.isEmpty() ? QByteArray("Object") : sonucTuru;
    b.icerik = true;
    b.tekrar = tekrar;
    m_kuyruk.append(b);
    if (m_hazir) kuyruguBosalt();
    else if (!m_soket) baglan();
    return b.istek;
}

void Baglanti::kuyruguBosalt()
{
    while (m_hazir && !m_kuyruk.isEmpty()) {
        Bekleyen b = m_kuyruk.takeFirst();
        // initConnection: ilk yanit gelene kadar her istek sarilir (sunucu istekleri sirasiz isleyebilir)
        if (!m_initTamam && !b.sarili) { b.govde = sarmala(b.ham); b.sarili = true; }
        const qint64 id = sifreliGonder(b.govde, b.icerik);
        m_bekleyen.insert(id, b);
    }
}

void Baglanti::yenidenGonder(qint64 eskiMsgId)
{
    if (!m_bekleyen.contains(eskiMsgId)) return;
    const Bekleyen b = m_bekleyen.take(eskiMsgId);
    const qint64 id = sifreliGonder(b.govde, b.icerik);
    m_bekleyen.insert(id, b);
}

void Baglanti::onayGonder()
{
    if (m_onay.isEmpty() || !m_hazir) return;
    TlYazici w;
    w.uint32(KMsgsAck);
    w.uint32(0x1cb5c415);
    w.int32(m_onay.size());
    for (int i = 0; i < m_onay.size(); ++i) w.int64(m_onay[i]);
    m_onay.clear();
    sifreliGonder(w.veri, false);
}

void Baglanti::pingAt()
{
    if (!m_hazir) return;
    TlYazici w;
    w.uint32(KPingDelay);
    w.int64(QDateTime::currentDateTime().toMSecsSinceEpoch());
    w.int32(75);
    sifreliGonder(w.veri, true);
}

void Baglanti::sifreliIsle(const QByteArray &p)
{
    if (m_anahtar.isEmpty() || p.size() < 24 + 16) return;
    if (p.left(8) != m_anahtarId) { emit gunluk(QString::fromLatin1("auth_key_id uyusmadi")); return; }
    const QByteArray msgKey = p.mid(8, 16);
    QByteArray key, iv;
    kdf(msgKey, 8, key, iv);
    const QByteArray d = Sifre::igeCoz(p.mid(24), key, iv);
    if (Sifre::sha256(m_anahtar.mid(96, 32) + d).mid(8, 16) != msgKey) { emit gunluk(QString::fromLatin1("msg_key uyusmadi")); return; }
    if (d.size() < 32) return;
    if (d.mid(8, 8) != m_oturum) { emit gunluk(QString::fromLatin1("oturum kimligi uyusmadi")); return; }
    const qint64 msgId = i64(d, 16);
    const qint32 seq = qint32(u32(d, 24));
    const qint32 n = qint32(u32(d, 28));
    if (n < 0 || 32 + n > d.size()) return;
    if (seq & 1) m_onay.append(msgId);
    isle(d.mid(32, n), msgId);
    onayGonder();
}

void Baglanti::isle(const QByteArray &govde, qint64 msgId)
{
    if (govde.size() < 4) return;
    const quint32 id = u32(govde, 0);
    if (id == KContainer) {
        TlOkuyucu r(govde);
        r.uint32();
        const int n = r.int32();
        for (int i = 0; i < n && !r.hata; ++i) {
            const qint64 ic = r.int64();
            const qint32 seq = r.int32();
            const qint32 boy = r.int32();
            const QByteArray g = r.ham(boy);
            if (r.hata) break;
            if (seq & 1) m_onay.append(ic);
            isle(g, ic);
        }
        return;
    }
    if (id == KGzip) {
        TlOkuyucu r(govde);
        r.uint32();
        const QByteArray ac = Sifre::gzipAc(r.bayt());
        if (!ac.isEmpty()) isle(ac, msgId);
        return;
    }
    if (id == KRpcResult) {
        if (govde.size() < 12) return;
        const qint64 istekMsg = i64(govde, 4);
        QByteArray sonucB = govde.mid(12);
        if (sonucB.size() >= 4 && u32(sonucB, 0) == KGzip) {
            TlOkuyucu z(sonucB);
            z.uint32();
            sonucB = Sifre::gzipAc(z.bayt());
        }
        const Bekleyen b = m_bekleyen.take(istekMsg);
        if (sonucB.size() >= 4 && u32(sonucB, 0) == KRpcError) {
            TlOkuyucu r(sonucB);
            r.uint32();
            const int kod = r.int32();
            const QString mesaj = r.dize();
            emit gunluk(QString::fromLatin1("DC%1 rpc hatasi %2 %3").arg(m_dc).arg(kod).arg(mesaj));
            if (mesaj == QLatin1String("CONNECTION_NOT_INITED") && b.istek && !b.sarili) {
                Bekleyen y = b;
                y.govde = sarmala(b.ham);
                y.sarili = true;
                m_bekleyen.insert(sifreliGonder(y.govde, true), y);
                return;
            }
            if (b.istek) emit hata(b.istek, kod, mesaj);
            return;
        }
        if (!b.istek) return;   // ping gibi takip edilmeyen istekler
        TlOkuyucu r(sonucB);
        const QVariant v = TlSema::tek().oku(r, b.tur);
        if (r.hata) {
            emit gunluk(QString::fromLatin1("yanit okunamadi (%1): %2").arg(QString::fromLatin1(b.tur)).arg(TlSema::tek().sonHata));
            emit hata(b.istek, -1, QString::fromLatin1("PARSE_ERROR ") + TlSema::tek().sonHata);
            return;
        }
        m_initTamam = true;
        emit sonuc(b.istek, v);
        return;
    }
    if (id == KBadSalt) {
        const qint64 kotu = i64(govde, 4);
        m_tuz = i64(govde, 20);
        emit gunluk(QString::fromLatin1("DC%1 yeni tuz").arg(m_dc));
        yenidenGonder(kotu);
        return;
    }
    if (id == KBadMsg) {
        const qint64 kotu = i64(govde, 4);
        const qint32 kod = qint32(u32(govde, 16));
        emit gunluk(QString::fromLatin1("DC%1 bad_msg_notification %2").arg(m_dc).arg(kod));
        if (kod == 16 || kod == 17) {
            m_saatFarki = int((msgId >> 32) - qint64(QDateTime::currentDateTime().toTime_t()));
            m_sonMsgId = 0;
        } else if (kod == 32) {
            m_seq += 64;
        } else if (kod == 33) {
            m_seq = qMax(0, m_seq - 16);
        }
        yenidenGonder(kotu);
        return;
    }
    if (id == KNewSession) {
        m_tuz = i64(govde, 20);
        return;
    }
    if (id == KMsgsAck || id == KDetayli || id == KYeniDetayli) return;
    if (id == KPong) {
        m_bekleyen.remove(i64(govde, 4));
        return;
    }
    // kendiliginden gelen guncellemeler (updates, updateShort, updateShortMessage ...)
    TlOkuyucu r(govde);
    const QVariant v = TlSema::tek().nesneOku(r);
    if (r.hata) { emit gunluk(QString::fromLatin1("guncelleme okunamadi: %1").arg(TlSema::tek().sonHata)); return; }
    emit guncelleme(v);
}
