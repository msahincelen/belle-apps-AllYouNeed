#include "telegram.h"
#include "depo.h"
#include "mtproto.h"
#include "tl.h"
#include "sifre.h"
#include "buyuksayi.h"
#include "opusses.h"
#include "dil.h"
#include <QtCore/QSettings>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QBuffer>
#include <QtCore/QTextStream>
#include <QtGui/QImage>
#include <QtGui/QImageReader>
#include <string.h>
#ifdef Q_OS_SYMBIAN
#include <QtNetwork/QNetworkConfigurationManager>
#include <QtNetwork/QNetworkSession>
#include <e32std.h>
#include <hal.h>
#endif

#define L1(s) QString::fromLatin1(s)
#define K(s) QString::fromLatin1(s)

static const int PARCA_IND = 131072;        // indirme parcasi (1 MB'yi boler, 4 KB'nin kati)
static const int PARCA_IND_BUYUK = 524288;  // 10 MB'den buyuk indirmelerde (daha az istek)
static const int PARCA_YUK = 131072;        // yukleme parcasi (en fazla 512 KB)
static const int PARCA_YUK_BUYUK = 524288;  // saveBigFilePart: en fazla 4000 parca x 512 KB
// Telegram'in Premium olmayan hesap siniri: 4000 parca x 512 KB = 2000 MB. Dosyalar bellege alinmaz,
// parca parca diskten okunur / diske yazilir (uygulama yigini 64 MB oldugu icin eskiden 40 MB'di).
static const qint64 EN_BUYUK_DOSYA = qint64(4000) * PARCA_YUK_BUYUK;

// Uretim veri merkezleri (help.getConfig ile dogrulandi, 15.11.2026)
static QString dcAdres(int dc)
{
    switch (dc) {
    case 1: return L1("149.154.175.53");
    case 2: return L1("149.154.167.41");
    case 3: return L1("149.154.175.100");
    case 4: return L1("149.154.167.92");
    case 5: return L1("91.108.56.130");
    }
    return L1("149.154.167.41");
}

static QVariantMap harita(const char *ad)
{
    QVariantMap m;
    m.insert(K("_"), L1(ad));
    return m;
}

static QString tip(const QVariant &v) { return v.toMap().value(K("_")).toString(); }
static qint64 uzun(const QVariant &v) { return v.toLongLong(); }
static qint64 dcIsAnahtari(int dc, int istek) { return (qint64(dc) << 32) | quint32(istek); }

static QString temizAd(const QString &ad)
{
    QString s;
    for (int i = 0; i < ad.size(); ++i) {
        const QChar c = ad.at(i);
        s.append(QString::fromLatin1("\\/:*?\"<>|").contains(c) || c.unicode() < 32 ? QChar(QLatin1Char('_')) : c);
    }
    return s.trimmed().left(80);
}

static QString mimeUzanti(const QString &mime)
{
    if (mime == L1("image/jpeg")) return L1(".jpg");
    if (mime == L1("image/png")) return L1(".png");
    if (mime == L1("image/gif")) return L1(".gif");
    if (mime == L1("video/mp4")) return L1(".mp4");
    if (mime == L1("video/3gpp")) return L1(".3gp");
    if (mime == L1("audio/mpeg")) return L1(".mp3");
    if (mime == L1("audio/ogg")) return L1(".ogg");
    if (mime == L1("audio/mp4") || mime == L1("audio/aac")) return L1(".m4a");
    if (mime == L1("application/pdf")) return L1(".pdf");
    if (mime == L1("application/zip")) return L1(".zip");
    return L1(".bin");
}

static QString uzantiMime(const QString &yol)
{
    const QString e = QFileInfo(yol).suffix().toLower();
    if (e == L1("jpg") || e == L1("jpeg")) return L1("image/jpeg");
    if (e == L1("png")) return L1("image/png");
    if (e == L1("gif")) return L1("image/gif");
    if (e == L1("mp4") || e == L1("m4v")) return L1("video/mp4");
    if (e == L1("3gp") || e == L1("3g2")) return L1("video/3gpp");
    if (e == L1("mp3")) return L1("audio/mpeg");
    if (e == L1("ogg")) return L1("audio/ogg");
    if (e == L1("m4a") || e == L1("aac")) return L1("audio/mp4");
    if (e == L1("amr")) return L1("audio/amr");
    if (e == L1("wav")) return L1("audio/wav");
    if (e == L1("pdf")) return L1("application/pdf");
    if (e == L1("txt")) return L1("text/plain");
    if (e == L1("zip")) return L1("application/zip");
    return L1("application/octet-stream");
}

// ======================================================================= yasam dongusu
Telegram::Telegram(const QString &veriKlasoru, const QString &gunlukDosyasi, QObject *parent)
    : QObject(parent), m_klasor(veriKlasoru), m_gunlukDosya(gunlukDosyasi), m_durum(L1("baslangic")), m_apiId(0), m_benId(0), m_anaDc(2), m_bagli(false),
      m_girisli(false), m_baglantiIstendi(false), m_b(0), m_ag(0), m_sonTarih(0), m_sonMesaj(0), m_yenidenDeneme(0),
      m_indAktif(false), m_yukAktif(false), m_oncekiCokme(false), m_sonCpu(0)
{
    QDir().mkpath(m_klasor);
    QDir().mkpath(medyaKlasoru());
    m_indZaman.setSingleShot(true);
    m_indZaman.setInterval(30000);
    connect(&m_indZaman, SIGNAL(timeout()), this, SLOT(indirmeZamanAsimi()));
    // onbellegi sinirla: en yeni 400 dosya
    const QFileInfoList l = QDir(medyaKlasoru()).entryInfoList(QDir::Files, QDir::Time);
    for (int i = 400; i < l.size(); ++i) QFile::remove(l.at(i).absoluteFilePath());

    // Cokme teshisi: calisirken isaret dosyasi durur, duzgun kapanista silinir. Acilista hala duruyorsa onceki
    // oturum yikicinin calismasina firsat olmadan bitmis demektir (panic / bellek). Bkz. basla() ve nabiz().
    m_oncekiCokme = QFile::exists(isaretDosyasi());
    QFile isaret(isaretDosyasi());
    if (isaret.open(QIODevice::WriteOnly)) { isaret.write(QDateTime::currentDateTime().toString(Qt::ISODate).toLatin1()); isaret.close(); }
    m_nabiz.setInterval(5 * 60000);   // 5 dk (dosya yazmasini azaltmak icin; kasma olcumu BelleIslemci ile)
    connect(&m_nabiz, SIGNAL(timeout()), this, SLOT(nabiz()));
    m_nabiz.start();
    m_gunlukZaman.setSingleShot(true);
    m_gunlukZaman.setInterval(60000);
    connect(&m_gunlukZaman, SIGNAL(timeout()), this, SLOT(gunlukYaz()));
}

Telegram::~Telegram()
{
    kaydet();
    kayit(L1("kapanis: duzgun (") + bellekOzeti() + L1(")"));   // "kapanis" acil: bekleyenlerle birlikte hemen yazilir
    gunlukYaz();
    QFile::remove(isaretDosyasi());
}

QString Telegram::isaretDosyasi() const { return m_klasor + L1("/calisiyor.isaret"); }

// Bellek ve ic listelerin boyutu: sizinti ya da telefonun bellegi azalinca uygulamanin kapatilmasi gorulsun
QString Telegram::bellekOzeti() const
{
    QString s;
#ifdef Q_OS_SYMBIAN
    TInt ayrilan = 0;
    User::AllocSize(ayrilan);
    TInt bos = 0, toplam = 0;
    HAL::Get(HALData::EMemoryRAMFree, bos);
    HAL::Get(HALData::EMemoryRAM, toplam);
    s = L1("yigin=%1KB/%2KB tel_bos=%3MB/%4MB ").arg(ayrilan / 1024).arg(User::Heap().Size() / 1024).arg(bos / 1048576).arg(toplam / 1048576);
#endif
    return s + L1("isler=%1 dcIsler=%2 kisi=%3 grup=%4 gorulen=%5 medya=%6 ozet=%7 indKuyruk=%8 yukKuyruk=%9")
        .arg(m_isler.size()).arg(m_dcIsler.size()).arg(m_kisiler.size()).arg(m_gruplar.size()).arg(m_gorulen.size())
        .arg(m_medya.size()).arg(m_ozet.size()).arg(m_indKuyruk.size()).arg(m_yukKuyruk.size());
}

// Kasma teshisi: son nabizdan beri gecen sure (120 sn'den cok fazlaysa olay dongusu tikanmis) ve
// uygulamanin harcadigi islemci suresi (bosta ~0; 100'e yakinsa uygulama donguye girip telefonu kasiyor).
void Telegram::nabiz()
{
    QString ek;
    const QDateTime simdi = QDateTime::currentDateTime();
    if (m_sonNabiz.isValid()) ek += L1(" aralik=%1sn").arg(m_sonNabiz.secsTo(simdi));
    m_sonNabiz = simdi;
#ifdef Q_OS_SYMBIAN
    TTimeIntervalMicroSeconds cpu(0);
    if (RThread().GetCpuTime(cpu) == KErrNone) {
        const qint64 us = cpu.Int64();
        if (m_sonCpu > 0) ek += L1(" cpu=%1sn").arg((us - m_sonCpu) / 1000000);
        m_sonCpu = us;
    }
#endif
    kayit(L1("nabiz: ") + bellekOzeti() + (m_bagli ? L1(" bagli") : L1(" bagli-degil")) + ek);
}

// Gunluk satirlari bellekte biriktirilir, dosyaya en fazla dakikada bir toplu yazilir: C:/Data altindaki her dosya
// degisikligi telefonun arama dizinleyicisini (cpixsearchserver) uyandiriyor, sik kucuk yazmalar telefonu kasiyordu.
// Onemli satirlar (acilis, uyari, kapanis, giris, hata) cokmede kaybolmasin diye hemen yazilir.
void Telegram::kayit(const QString &s)
{
    const QString satir = QDateTime::currentDateTime().toString(L1("hh:mm:ss ")) + s;
    m_gunluk.append(satir);
    while (m_gunluk.size() > 300) m_gunluk.removeFirst();
    m_yazilacak.append(satir);
    const bool acil = s.startsWith(L1("basla")) || s.startsWith(L1("UYARI")) || s.startsWith(L1("kapanis")) ||
                      s.startsWith(L1("giris")) || s.startsWith(L1("hata")) || s.startsWith(L1("api"));
    if (acil || m_yazilacak.size() > 200) gunlukYaz();
    else if (!m_gunlukZaman.isActive()) m_gunlukZaman.start();
}

void Telegram::gunlukYaz()
{
    m_gunlukZaman.stop();
    if (m_yazilacak.isEmpty()) return;
    const QString yol = m_gunlukDosya.isEmpty() ? m_klasor + L1("/gunluk.txt") : m_gunlukDosya;
    QFile f(yol);
    if (f.open(QIODevice::Append | QIODevice::Text)) {
        if (f.size() > 200000) {
            // dolunca silme: bir onceki parcayi .1 olarak sakla (cokme oncesi kayitlar kaybolmasin)
            f.close();
            QFile::remove(yol + L1(".1"));
            QFile::rename(yol, yol + L1(".1"));
            f.setFileName(yol);
            f.open(QIODevice::WriteOnly | QIODevice::Text);
        }
        QTextStream t(&f);
        for (int i = 0; i < m_yazilacak.size(); ++i) t << m_yazilacak.at(i) << "\n";
    }
    m_yazilacak.clear();
}

void Telegram::durumYap(const QString &d)
{
    if (m_durum == d) return;
    m_durum = d;
    kayit(L1("durum: ") + d);
    emit durumDegisti();
}

void Telegram::kaydet()
{
    QSettings s(m_klasor + L1("/oturum.ini"), QSettings::IniFormat);
    s.setValue(L1("anaDc"), m_anaDc);
    s.setValue(L1("girisli"), m_girisli);
    s.setValue(L1("benId"), QString::number(m_benId));
    s.setValue(L1("benAd"), m_benAd);
    s.setValue(L1("telefon"), m_telefon);
    if (m_b && !m_b->anahtar().isEmpty()) {
        s.setValue(L1("dc%1/anahtar").arg(m_b->dc()), QString::fromLatin1(m_b->anahtar().toBase64()));
        s.setValue(L1("dc%1/tuz").arg(m_b->dc()), QString::number(m_b->tuz()));
    }
}

void Telegram::basla()
{
    QSettings s(m_klasor + L1("/oturum.ini"), QSettings::IniFormat);
    m_anaDc = s.value(L1("anaDc"), 2).toInt();
    m_girisli = s.value(L1("girisli"), false).toBool();
    m_benId = s.value(L1("benId")).toString().toLongLong();
    m_benAd = s.value(L1("benAd")).toString();
    m_telefon = s.value(L1("telefon")).toString();
    apiOku();
    kayit(L1("basla: dc%1 girisli=%2 api=%3 surum=0.3.1").arg(m_anaDc).arg(m_girisli).arg(m_apiId ? L1("var") : L1("yok")));
    if (m_oncekiCokme) { kayit(L1("UYARI: onceki oturum duzgun kapanmadi (cokme ya da sistem kapatti)")); m_oncekiCokme = false; }
    kayit(L1("nabiz: ") + bellekOzeti());
    if (!m_apiId || m_apiHash.isEmpty()) { durumYap(L1("api")); return; }   // apiAyarla() basla()'yi yeniden cagirir
    durumYap(L1("baglaniyor"));
    if (m_ag) { agAcildi(); return; }   // ag oturumu zaten acik (API sonradan girildi)
#ifdef Q_OS_SYMBIAN
    // Symbian'da ham soket baglantiyi kendisi acmaz: once ag oturumu
    QNetworkConfigurationManager yon;
    m_ag = new QNetworkSession(yon.defaultConfiguration(), this);
    connect(m_ag, SIGNAL(opened()), this, SLOT(agAcildi()));
    m_ag->open();
    QTimer::singleShot(15000, this, SLOT(agAcildi()));   // ag olayi gelmezse yine de dene
#else
    agAcildi();
#endif
}

// ======================================================================= API kimligi
// Kullanicinin kendi uygulama kimligi (https://my.telegram.org -> API development tools).
// Ozel klasorde ayri dosyada: cikis (oturum.ini temizligi) bunu silmez.
void Telegram::apiOku()
{
    QSettings k(m_klasor + L1("/kimlik.ini"), QSettings::IniFormat);
    m_apiId = k.value(L1("api_id")).toString().toInt();
    m_apiHash = k.value(L1("api_hash")).toString();
}

QString Telegram::apiAyarla(const QString &id, const QString &hash)
{
    QString i, h;
    for (int n = 0; n < id.size(); ++n) if (id.at(n).isDigit()) i.append(id.at(n));
    for (int n = 0; n < hash.size(); ++n) { const QChar c = hash.at(n).toLower(); if (c.isDigit() || (c >= QLatin1Char('a') && c <= QLatin1Char('f'))) h.append(c); }
    bool tamam = false;
    const int sayi = i.toInt(&tamam);
    if (!tamam || sayi <= 0) return Dil::t("api_id yalnızca rakamlardan oluşmalı.");
    if (h.size() != 32) return Dil::t("api_hash 32 karakter olmalı (0-9, a-f). Şu an: %1").arg(h.size());
    QSettings k(m_klasor + L1("/kimlik.ini"), QSettings::IniFormat);
    k.setValue(L1("api_id"), QString::number(sayi));
    k.setValue(L1("api_hash"), h);
    k.sync();
    m_apiId = sayi;
    m_apiHash = h;
    kayit(L1("api kimligi kaydedildi"));   // degerler gunluge yazilmaz
    if (m_durum == L1("api")) {
        if (m_b) { initAyarla(m_b); durumYap(m_girisli ? L1("baglaniyor") : L1("telefon")); }
        else basla();
    }
    return QString();
}

void Telegram::apiDegistir()
{
    {
        QSettings k(m_klasor + L1("/kimlik.ini"), QSettings::IniFormat);
        k.clear();
    }
    if (m_girisli) cikis();
    m_apiId = 0;
    m_apiHash.clear();
    durumYap(L1("api"));
}

void Telegram::initAyarla(Baglanti *b) const
{
    Baglanti::InitBilgi ib;
    ib.apiId = m_apiId;
    ib.cihaz = L1("Nokia E6");
    ib.sistem = L1("Symbian Belle");
    ib.surum = L1("BelleGram 0.3.1");
    ib.dil = Dil::tek() ? Dil::tek()->kod() : L1("tr");
    b->initAyarla(ib);
}

void Telegram::agAcildi()
{
    if (m_baglantiIstendi) return;
    m_baglantiIstendi = true;
    baglantiKur(m_anaDc);
    if (!m_girisli) durumYap(L1("telefon"));
}

void Telegram::baglantiKur(int dc)
{
    if (m_b) { m_b->kapat(); m_b->deleteLater(); m_b = 0; }
    m_isler.clear();
    m_anaDc = dc;
    m_b = new Baglanti(dc, dcAdres(dc), 443, false, this);
    initAyarla(m_b);
    QSettings s(m_klasor + L1("/oturum.ini"), QSettings::IniFormat);
    const QByteArray a = QByteArray::fromBase64(s.value(L1("dc%1/anahtar").arg(dc)).toString().toLatin1());
    if (a.size() == 256) m_b->anahtarAyarla(a, s.value(L1("dc%1/tuz").arg(dc)).toString().toLongLong());
    connect(m_b, SIGNAL(anahtarHazir()), this, SLOT(anahtarHazir()));
    connect(m_b, SIGNAL(sonuc(int,QVariant)), this, SLOT(sonuc(int,QVariant)));
    connect(m_b, SIGNAL(hata(int,int,QString)), this, SLOT(hata(int,int,QString)));
    connect(m_b, SIGNAL(guncelleme(QVariant)), this, SLOT(guncelleme(QVariant)));
    connect(m_b, SIGNAL(koptu(QString)), this, SLOT(koptu(QString)));
    connect(m_b, SIGNAL(gunluk(QString)), this, SLOT(kayit(QString)));
    m_b->baglan();
    // ana merkez degistiyse onun medya baglantisi gereksiz
    if (m_dcB.contains(dc)) { Baglanti *eski = m_dcB.take(dc); eski->kapat(); eski->deleteLater(); }
}

void Telegram::anahtarHazir()
{
    kaydet();
    m_yenidenDeneme = 0;
    if (!m_bagli) { m_bagli = true; emit bagliDegisti(); }
    if (!m_bekleyenKod.isEmpty()) {
        // veri merkezi degisti: giris istegini yeni merkezde tekrarla
        const QString t = m_bekleyenKod;
        m_bekleyenKod.clear();
        m_sonKodIstegi = QDateTime();   // ic tekrar: 20 sn korumasina takilmasin
        kodGonder(t);
        return;
    }
    if (m_girisli) {
        cagir(L1("updates.getState"), QVariantMap(), IsDurum);   // guncellemeler bu oturuma akmaya baslasin
        if (m_durum != L1("hazir")) { durumYap(L1("hazir")); sohbetleriYukle(false); }
        indirmeBaslat();
        yuklemeBaslat();
    }
}

void Telegram::koptu(const QString &neden)
{
    if (m_bagli) { m_bagli = false; emit bagliDegisti(); }
    kayit(L1("koptu: ") + neden);
    ++m_yenidenDeneme;
#ifdef Q_OS_SYMBIAN
    // Wi-Fi uyuyunca / ag degisince Symbian ag oturumu kapanir; yeni soketler "Invalid socket descriptor"
    // verir ve oturum yeniden acilmadan baglanti hic kurulamaz (gunlukte saatlerce 900+ deneme gorulmustu).
    if (m_ag && (m_yenidenDeneme == 2 || m_yenidenDeneme % 5 == 0 || neden.contains(L1("descriptor"), Qt::CaseInsensitive))) {
        kayit(L1("ag oturumu yeniden aciliyor (durum %1)").arg(int(m_ag->state())));
        m_ag->close();
        m_ag->open();
    }
#endif
    QTimer::singleShot(qMin(30000, 2000 * m_yenidenDeneme), this, SLOT(yenidenBaglan()));
}

void Telegram::yenidenBaglan()
{
    if (m_b && !m_b->hazir()) m_b->baglan();
}

int Telegram::cagir(const QString &yontem, const QVariantMap &p, int tur, const QString &sohbet, qint64 ek, bool daha)
{
    if (!m_b) return 0;
    QByteArray sonucTuru;
    const QByteArray g = TlSema::tek().cagri(yontem, p, &sonucTuru);
    if (g.isEmpty()) { kayit(L1("serilestirilemedi: ") + yontem + L1(" ") + TlSema::tek().sonHata); return 0; }
    // Giris istekleri baglanti kopunca kendiliginden tekrarlanmaz: her auth.sendCode yeni kod uretip elde
    // edilen kodu gecersiz kilar (PHONE_CODE_INVALID) ve ust uste gonderim FLOOD_WAIT cezasi getirir.
    const bool tekrar = !(yontem == L1("auth.sendCode") || yontem == L1("auth.resendCode") || yontem == L1("auth.signIn") || yontem == L1("auth.checkPassword"));
    const int id = m_b->gonder(g, sonucTuru, tekrar);
    Is is;
    is.tur = tur; is.sohbet = sohbet; is.ek = ek; is.daha = daha;
    m_isler.insert(id, is);
    return id;
}

qint64 Telegram::rastgeleId() const
{
    qint64 r = 0;
    const QByteArray b = Sifre::rastgele(8);
    memcpy(&r, b.constData(), 8);
    return r;
}

// ======================================================================= giris
void Telegram::kodGonder(const QString &telefon)
{
    QString t;
    for (int i = 0; i < telefon.size(); ++i) if (telefon.at(i).isDigit()) t.append(telefon.at(i));
    if (t.size() < 8) { emit hataOldu(Dil::t("Telefon numarasını ülke koduyla yazın (örn. 905321234567).")); return; }
    // ayni numaraya 20 sn icinde ikinci kod istegi gonderme (cift basma / ust uste deneme FLOOD_WAIT getirir).
    // Veri merkezi yonlendirmesindeki ic tekrar bunu sifirlar (anahtarHazir).
    if (t == m_sonKodNumara && m_sonKodIstegi.isValid() && m_sonKodIstegi.secsTo(QDateTime::currentDateTime()) < 20) {
        kayit(L1("kod istegi atlandi (20 sn dolmadi)"));
        emit hataOldu(Dil::t("Kod az önce istendi. Gelmesini birkaç saniye bekleyin."));
        return;
    }
    m_sonKodNumara = t;
    m_sonKodIstegi = QDateTime::currentDateTime();
    m_telefon = t;
    QVariantMap p;
    p.insert(K("phone_number"), L1("+") + t);
    p.insert(K("api_id"), m_apiId);
    p.insert(K("api_hash"), m_apiHash);
    p.insert(K("settings"), harita("codeSettings"));
    cagir(L1("auth.sendCode"), p, IsKod);
    emit bilgi(Dil::t("Kod isteniyor…"));
}

void Telegram::kodOnayla(const QString &kod)
{
    QString k;
    for (int i = 0; i < kod.size(); ++i) if (kod.at(i).isDigit()) k.append(kod.at(i));
    if (k.isEmpty()) return;
    // ayni kodu 8 sn icinde ikinci kez gonderme (Enter'a/tamam'a ust uste basma)
    static QString sonKod;
    static QDateTime sonGonderim;
    if (k == sonKod && sonGonderim.isValid() && sonGonderim.secsTo(QDateTime::currentDateTime()) < 8) return;
    sonKod = k;
    sonGonderim = QDateTime::currentDateTime();
    QVariantMap p;
    p.insert(K("phone_number"), L1("+") + m_telefon);
    p.insert(K("phone_code_hash"), m_kodHash);
    p.insert(K("phone_code"), k);
    cagir(L1("auth.signIn"), p, IsImza);
}

void Telegram::sifreOnayla(const QString &sifre)
{
    m_parola = sifre;
    cagir(L1("account.getPassword"), QVariantMap(), IsSifreBilgi);
    emit bilgi(Dil::t("Şifre doğrulanıyor (birkaç saniye sürebilir)…"));
}

void Telegram::numaraDegistir()
{
    m_kodHash.clear();
    durumYap(L1("telefon"));
}

void Telegram::cikis()
{
    if (m_girisli) cagir(L1("auth.logOut"), QVariantMap(), IsCikis);
    m_girisli = false;
    m_benId = 0;
    m_benAd.clear();
    QSettings s(m_klasor + L1("/oturum.ini"), QSettings::IniFormat);
    s.clear();
    m_kisiler.clear(); m_gruplar.clear(); m_gorulen.clear(); m_medya.clear(); m_ozet.clear();
    m_indKuyruk.clear(); m_yukKuyruk.clear(); m_indAktif = false; m_yukAktif = false;
    foreach (Baglanti *b, m_dcB) { b->kapat(); b->deleteLater(); }
    m_dcB.clear(); m_dcHazir.clear(); m_dcIsler.clear();
    durumYap(L1("telefon"));
    // logOut gitsin, sonra yeni anahtarla bastan (ayarlar silindi: anahtar yeniden uretilir)
    QTimer::singleShot(2000, this, SLOT(yeniOturum()));
}

void Telegram::yeniOturum()
{
    baglantiKur(2);
}

void Telegram::girisTamam(const QVariantMap &user)
{
    kisileriAl(QVariantList() << user);
    m_benId = uzun(user.value(K("id")));
    m_benAd = (user.value(K("first_name")).toString() + L1(" ") + user.value(K("last_name")).toString()).trimmed();
    m_girisli = true;
    m_parola.clear();
    kaydet();
    kayit(L1("giris tamam"));
    cagir(L1("updates.getState"), QVariantMap(), IsDurum);
    durumYap(L1("hazir"));
    sohbetleriYukle(false);
}

// SRP (iki adimli dogrulama): https://core.telegram.org/api/srp
void Telegram::sifreHesapla(const QVariantMap &pb)
{
    const QVariantMap algo = pb.value(K("current_algo")).toMap();
    if (algo.value(K("_")).toString() != L1("passwordKdfAlgoSHA256SHA256PBKDF2HMACSHA512iter100000SHA256ModPow")) {
        emit hataOldu(Dil::t("Bu şifre türü desteklenmiyor."));
        return;
    }
    const QByteArray salt1 = algo.value(K("salt1")).toByteArray();
    const QByteArray salt2 = algo.value(K("salt2")).toByteArray();
    const int g = algo.value(K("g")).toInt();
    const QByteArray pB = algo.value(K("p")).toByteArray();
    const QByteArray srpB = pB.size() ? pb.value(K("srp_B")).toByteArray() : QByteArray();
    const BuyukSayi p = BuyukSayi::baytlardan(pB);
    const BuyukSayi B = BuyukSayi::baytlardan(srpB);

    const QByteArray parola = m_parola.toUtf8();
    #define SH(d, s) Sifre::sha256((s) + (d) + (s))
    const QByteArray ph1 = SH(SH(parola, salt1), salt2);
    const QByteArray ph2 = SH(Sifre::pbkdf2Sha512(ph1, salt1, 100000), salt2);
    #undef SH
    const BuyukSayi x = BuyukSayi::baytlardan(ph2);
    const BuyukSayi gS = BuyukSayi(quint32(g));
    const QByteArray gPad = gS.baytlar(256);
    const QByteArray bPad = B.baytlar(256);
    const BuyukSayi k = BuyukSayi::baytlardan(Sifre::sha256(pB + gPad));
    QByteArray aPad;
    BuyukSayi a, A;
    do {
        a = BuyukSayi::baytlardan(Sifre::rastgele(256));
        A = BuyukSayi::usMod(gS, a, p);
        aPad = A.baytlar(256);
    } while (A.sifir());
    const BuyukSayi u = BuyukSayi::baytlardan(Sifre::sha256(aPad + bPad));
    const BuyukSayi v = BuyukSayi::usMod(gS, x, p);
    const BuyukSayi kv = BuyukSayi::mod(BuyukSayi::carp(k, v), p);
    const BuyukSayi Bm = BuyukSayi::mod(B, p);
    const BuyukSayi t = (Bm >= kv) ? BuyukSayi::cikar(Bm, kv) : BuyukSayi::cikar(BuyukSayi::topla(Bm, p), kv);
    const BuyukSayi us = BuyukSayi::topla(a, BuyukSayi::carp(u, x));
    const QByteArray sA = BuyukSayi::usMod(t, us, p).baytlar(256);
    const QByteArray kA = Sifre::sha256(sA);
    const QByteArray hp = Sifre::sha256(pB), hg = Sifre::sha256(gPad);
    QByteArray xr(32, 0);
    for (int i = 0; i < 32; ++i) xr[i] = char(hp.at(i) ^ hg.at(i));
    const QByteArray m1 = Sifre::sha256(xr + Sifre::sha256(salt1) + Sifre::sha256(salt2) + aPad + bPad + kA);

    QVariantMap girdi = harita("inputCheckPasswordSRP");
    girdi.insert(K("srp_id"), pb.value(K("srp_id")));
    girdi.insert(K("A"), aPad);
    girdi.insert(K("M1"), m1);
    QVariantMap p2;
    p2.insert(K("password"), girdi);
    m_parola.clear();
    cagir(L1("auth.checkPassword"), p2, IsSifre);
}

// ======================================================================= sonuclar
void Telegram::sonuc(int istek, const QVariant &v)
{
    if (!m_isler.contains(istek)) return;
    const Is is = m_isler.take(istek);
    const QVariantMap m = v.toMap();
    const QString t = m.value(K("_")).toString();
    switch (is.tur) {
    case IsKod: {
        if (t == L1("auth.sentCodeSuccess")) { girisTamam(m.value(K("authorization")).toMap().value(K("user")).toMap()); return; }
        m_kodHash = m.value(K("phone_code_hash")).toString();
        const QString kt = tip(m.value(K("type")));
        kayit(L1("kod gonderildi: ") + kt + L1(" sonraki=") + tip(m.value(K("next_type"))) + L1(" sure=") + m.value(K("timeout")).toString());
        if (kt == L1("auth.sentCodeTypeApp")) m_kodBilgi = Dil::t("Kod, Telegram uygulamanıza (başka bir cihazdaki) mesaj olarak gönderildi.");
        else if (kt == L1("auth.sentCodeTypeSms")) m_kodBilgi = Dil::t("Kod SMS ile gönderildi.");
        else if (kt == L1("auth.sentCodeTypeCall")) m_kodBilgi = Dil::t("Kod sesli arama ile söylenecek.");
        else m_kodBilgi = Dil::t("Kod gönderildi.");
        durumYap(L1("kod"));
        emit durumDegisti();
        return;
    }
    case IsImza:
    case IsSifre:
        if (t == L1("auth.authorizationSignUpRequired")) { emit hataOldu(Dil::t("Bu numarayla bir Telegram hesabı yok. Önce resmi uygulamadan kaydolun.")); return; }
        girisTamam(m.value(K("user")).toMap());
        return;
    case IsSifreBilgi:
        m_ipucu = m.value(K("hint")).toString();
        if (m_parola.isEmpty()) { emit durumDegisti(); return; }   // yalnizca ipucu istendi
        // PBKDF2 100.000 tur telefonda birkac saniye surer
        sifreHesapla(m);
        return;
    case IsSohbet: {
        kisileriAl(m.value(K("users")).toList());
        sohbetleriAl(m.value(K("chats")).toList());
        QHash<QString, QVariantMap> mesajlar;
        const QVariantList ml = m.value(K("messages")).toList();
        for (int i = 0; i < ml.size(); ++i) {
            const QVariantMap mm = ml[i].toMap();
            mesajlar.insert(anahtar(mm.value(K("peer_id"))) + L1(":") + QString::number(mm.value(K("id")).toInt()), mm);
        }
        const uint simdi = QDateTime::currentDateTime().toTime_t();
        QVariantList liste;
        const QVariantList dl = m.value(K("dialogs")).toList();
        for (int i = 0; i < dl.size(); ++i) {
            const QVariantMap d = dl[i].toMap();
            if (d.value(K("_")).toString() != L1("dialog")) continue;
            const QString key = anahtar(d.value(K("peer")));
            const QVariantMap mm = mesajlar.value(key + L1(":") + QString::number(d.value(K("top_message")).toInt()));
            const QVariantMap h = mesajHarita(mm);
            QVariantMap o = sohbetOge(key);
            o.insert(K("son"), mesajOzet(h));
            o.insert(K("zaman"), h.value(K("zaman")));
            o.insert(K("okunmamis"), d.value(K("unread_count")).toInt());
            o.insert(K("benden"), h.value(K("benden")));
            o.insert(K("sabit"), d.value(K("pinned")).toBool());
            const uint sus = d.value(K("notify_settings")).toMap().value(K("mute_until")).toUInt();
            o.insert(K("sessiz"), sus > simdi);
            m_okunanCikis.insert(key, d.value(K("read_outbox_max_id")).toInt());
            liste.append(o);
            if (!mm.isEmpty()) {
                m_sonTarih = mm.value(K("date")).toInt();
                m_sonMesaj = mm.value(K("id")).toInt();
                m_sonPeer = girdiPeer(key);
            }
            if (h.value(K("id")).toInt() > m_enYeni.value(key)) m_enYeni.insert(key, h.value(K("id")).toInt());
        }
        const bool sonu = t == L1("messages.dialogs") || dl.size() < 30;
        emit sohbetListesi(liste, is.daha, sonu);
        return;
    }
    case IsGecmis: {
        kisileriAl(m.value(K("users")).toList());
        sohbetleriAl(m.value(K("chats")).toList());
        const QVariantList ml = m.value(K("messages")).toList();
        QList<QVariantMap> hl;
        for (int i = 0; i < ml.size(); ++i) {
            const QVariantMap mm = ml[i].toMap();
            if (mm.value(K("_")).toString() == L1("messageEmpty")) continue;
            hl.append(mesajHarita(mm));   // once hepsi (yanit alintilari icin onbellege girsin)
        }
        const int okunan = m_okunanCikis.value(is.sohbet);
        QVariantList liste;
        for (int i = 0; i < hl.size(); ++i) {
            QVariantMap h = hl[i];
            yanitDoldur(h, is.sohbet);
            const int id = h.value(K("id")).toInt();
            if (h.value(K("benden")).toBool()) h.insert(K("okundu"), id <= okunan);
            m_gorulen.insert(is.sohbet + L1(":") + QString::number(id));
            if (!m_enEski.contains(is.sohbet) || id < m_enEski.value(is.sohbet)) m_enEski.insert(is.sohbet, id);
            if (id > m_enYeni.value(is.sohbet)) m_enYeni.insert(is.sohbet, id);
            liste.append(h);
        }
        emit mesajListesi(is.sohbet, liste, is.daha, ml.size() < 30);
        return;
    }
    case IsGonder: {
        const QString yer = m_rastgele.take(is.ek);
        const QString gecici = yer.section(QLatin1Char('|'), 1);
        if (t == L1("updateShortSentMessage")) {
            QVariantMap h;
            h.insert(K("id"), QString::number(m.value(K("id")).toInt()));
            h.insert(K("zaman"), double(m.value(K("date")).toUInt()) * 1000.0);
            m_gorulen.insert(is.sohbet + L1(":") + h.value(K("id")).toString());
            emit mesajGitti(is.sohbet, gecici, h);
            return;
        }
        // updates / updatesCombined: updateMessageID ile yeni kimlik, updateNewMessage ile mesaj
        kisileriAl(m.value(K("users")).toList());
        sohbetleriAl(m.value(K("chats")).toList());
        const QVariantList ul = m.value(K("updates")).toList();
        QVariantMap bulunan;
        for (int i = 0; i < ul.size(); ++i) {
            const QVariantMap u = ul[i].toMap();
            const QString ut = u.value(K("_")).toString();
            if (ut == L1("updateNewMessage") || ut == L1("updateNewChannelMessage")) {
                const QVariantMap mm = u.value(K("message")).toMap();
                if (mm.value(K("out")).toBool() || bulunan.isEmpty()) bulunan = mesajHarita(mm);
            } else if (ut != L1("updateMessageID")) {
                tekGuncelleme(u);
            }
        }
        if (!bulunan.isEmpty()) {
            yanitDoldur(bulunan, is.sohbet);
            m_gorulen.insert(is.sohbet + L1(":") + bulunan.value(K("id")).toString());
            emit mesajGitti(is.sohbet, gecici, bulunan);
            QVariantMap o;
            o.insert(K("id"), is.sohbet);
            o.insert(K("ad"), sohbetAdi(is.sohbet));
            o.insert(K("son"), mesajOzet(bulunan));
            o.insert(K("zaman"), bulunan.value(K("zaman")));
            o.insert(K("benden"), true);
            emit sohbetGuncellendi(o);
        }
        return;
    }
    case IsGuncelleme:
        if (t.startsWith(L1("update"))) guncelleme(v);
        return;
    case IsKisiler: {
        kisileriAl(m.value(K("users")).toList());
        QVariantList liste;
        const QVariantList cl = m.value(K("contacts")).toList();
        for (int i = 0; i < cl.size(); ++i) {
            const qint64 id = uzun(cl[i].toMap().value(K("user_id")));
            if (m_kisiler.contains(id)) liste.append(kisiOge(id));
        }
        emit kisiListesi(liste);
        return;
    }
    case IsAra: {
        kisileriAl(m.value(K("users")).toList());
        sohbetleriAl(m.value(K("chats")).toList());
        QVariantList liste;
        QSet<QString> eklenen;
        QVariantList peers = m.value(K("my_results")).toList();
        peers += m.value(K("results")).toList();
        for (int i = 0; i < peers.size(); ++i) {
            const QString key = anahtar(peers[i]);
            if (key.isEmpty() || eklenen.contains(key)) continue;
            eklenen.insert(key);
            liste.append(key.at(0) == QLatin1Char('u') ? kisiOge(key.mid(1).toLongLong()) : sohbetOge(key));
        }
        emit aramaSonucu(is.metin, liste);
        return;
    }
    case IsDosya:
        dosyaParcasi(is, m);
        return;
    case IsDisaAktar: {
        QVariantMap p;
        p.insert(K("id"), m.value(K("id")));
        p.insert(K("bytes"), m.value(K("bytes")));
        dcCagir(int(is.ek), L1("auth.importAuthorization"), p, IsIceAktar);
        return;
    }
    case IsParca:
        if (!m_yukAktif || is.ek != m_yuk.rastgele) return;
        ++m_yuk.parca;
        emit yuklemeIlerleme(m_yuk.sohbet, m_yuk.gecici, m_yuk.parca * 100 / qMax(1, m_yuk.toplam));
        if (m_yuk.parca < m_yuk.toplam) { parcaGonder(); return; }
        {
            QVariantMap dosya = harita(m_yuk.buyuk ? "inputFileBig" : "inputFile");
            dosya.insert(K("id"), m_yuk.dosyaId);
            dosya.insert(K("parts"), m_yuk.toplam);
            dosya.insert(K("name"), m_yuk.ad);
            if (!m_yuk.buyuk) dosya.insert(K("md5_checksum"), QString());
            QVariantMap medya;
            if (m_yuk.tur == L1("foto")) {
                medya = harita("inputMediaUploadedPhoto");
                medya.insert(K("file"), dosya);
            } else {
                medya = harita("inputMediaUploadedDocument");
                medya.insert(K("file"), dosya);
                medya.insert(K("mime_type"), m_yuk.mime);
                QVariantList at;
                if (m_yuk.tur == L1("ses")) {
                    QVariantMap a = harita("documentAttributeAudio");
                    a.insert(K("voice"), true);
                    a.insert(K("duration"), m_yuk.sure);
                    if (!m_yuk.dalga.isEmpty()) a.insert(K("waveform"), m_yuk.dalga);
                    at.append(a);
                } else {
                    QVariantMap a = harita("documentAttributeFilename");
                    a.insert(K("file_name"), m_yuk.ad);
                    at.append(a);
                    medya.insert(K("force_file"), true);
                }
                medya.insert(K("attributes"), at);
            }
            QVariantMap p;
            p.insert(K("peer"), girdiPeer(m_yuk.sohbet));
            p.insert(K("media"), medya);
            p.insert(K("message"), m_yuk.aciklama);
            p.insert(K("random_id"), m_yuk.rastgele);
            if (!m_yuk.yanit.isEmpty()) p.insert(K("reply_to"), yanitGirdi(m_yuk.yanit));
            cagir(L1("messages.sendMedia"), p, IsGonder, m_yuk.sohbet, m_yuk.rastgele);
            yuklemeBitti(QString());
        }
        return;
    case IsDurum:
    case IsOkundu:
    case IsCikis:
    case IsBos:
    default:
        return;
    }
}

void Telegram::hata(int istek, int kod, const QString &mesaj)
{
    const Is is = m_isler.take(istek);
    kayit(L1("hata %1 %2 (is %3)").arg(kod).arg(mesaj).arg(is.tur));
    // medya islerinin hatalari: giris yonlendirmesiyle karismasin
    if (is.tur == IsDosya) {
        if (mesaj.startsWith(L1("FILE_MIGRATE_")) && m_indAktif) {
            m_ind.dc = mesaj.section(QLatin1Char('_'), -1).toInt();
            m_ind.alinan = 0;
            m_indKuyruk.prepend(m_ind);
            m_indAktif = false;
            indirmeBaslat();
            return;
        }
        if (m_indAktif && is.metin == m_ind.anahtar) indirmeBitti(hataMetni(mesaj));
        return;
    }
    if (is.tur == IsDisaAktar) { if (m_indAktif) indirmeBitti(hataMetni(mesaj)); return; }
    if (is.tur == IsParca) { if (m_yukAktif && is.ek == m_yuk.rastgele) yuklemeBitti(hataMetni(mesaj)); return; }
    if (is.tur == IsBos || is.tur == IsOkundu || is.tur == IsDurum) return;
    // veri merkezi yonlendirmesi: PHONE_MIGRATE_4, USER_MIGRATE_4, NETWORK_MIGRATE_4
    if (kod == 303 && mesaj.contains(L1("_MIGRATE_"))) {
        const int dc = mesaj.section(QLatin1Char('_'), -1).toInt();
        if (dc >= 1 && dc <= 5) {
            kayit(L1("veri merkezi degisiyor: dc%1").arg(dc));
            if (is.tur == IsKod) m_bekleyenKod = m_telefon;
            baglantiKur(dc);
            kaydet();
            return;
        }
    }
    if (mesaj == L1("API_ID_INVALID") || mesaj == L1("API_ID_PUBLISHED_FLOOD")) {
        // girilen kimlik kabul edilmedi: API ekranina don (oturum yoksa kaybedilecek bir sey yok)
        QSettings k(m_klasor + L1("/kimlik.ini"), QSettings::IniFormat);
        k.clear();
        m_apiId = 0;
        m_apiHash.clear();
        durumYap(L1("api"));
        emit hataOldu(hataMetni(mesaj));
        return;
    }
    if (mesaj == L1("SESSION_PASSWORD_NEEDED")) {
        // sifre ekranina gec; ipucu icin parola bilgisini iste (sifre girilince yeniden istenir)
        durumYap(L1("sifre"));
        cagir(L1("account.getPassword"), QVariantMap(), IsSifreBilgi);
        return;
    }
    if (kod == 401 && (mesaj == L1("AUTH_KEY_UNREGISTERED") || mesaj == L1("SESSION_REVOKED") || mesaj == L1("USER_DEACTIVATED"))) {
        m_girisli = false;
        kaydet();
        durumYap(L1("telefon"));
        emit hataOldu(Dil::t("Oturum kapandı. Yeniden giriş yapın."));
        return;
    }
    if (is.tur == IsGonder) {
        const QString yer = m_rastgele.take(is.ek);
        emit mesajGitmedi(is.sohbet, yer.section(QLatin1Char('|'), 1), hataMetni(mesaj));
        return;
    }
    if (is.tur == IsAra) { emit aramaSonucu(is.metin, QVariantList()); return; }
    if (is.tur == IsSifreBilgi && m_durum == L1("sifre") && m_parola.isEmpty()) return;   // yalnizca ipucu istegi
    emit hataOldu(hataMetni(mesaj));
}

QString Telegram::hataMetni(const QString &k)
{
    if (k.startsWith(L1("FLOOD_WAIT_"))) {
        const int sn = k.mid(11).toInt();
        return Dil::t("Çok fazla deneme. %1 dakika sonra tekrar deneyin.").arg(qMax(1, (sn + 59) / 60));
    }
    if (k == L1("PHONE_NUMBER_INVALID")) return Dil::t("Telefon numarası geçersiz.");
    if (k == L1("BAGLANTI_KOPTU")) return Dil::t("Bağlantı koptu. Lütfen tekrar deneyin.");
    if (k == L1("PHONE_CODE_INVALID")) return Dil::t("Kod yanlış.");
    if (k == L1("PHONE_CODE_EXPIRED")) return Dil::t("Kodun süresi doldu. Yeni kod isteyin.");
    if (k == L1("PASSWORD_HASH_INVALID")) return Dil::t("Şifre yanlış.");
    if (k == L1("PHONE_NUMBER_BANNED")) return Dil::t("Bu numara Telegram tarafından engellenmiş.");
    if (k == L1("CHAT_WRITE_FORBIDDEN")) return Dil::t("Bu sohbete yazma izniniz yok.");
    if (k == L1("PEER_ID_INVALID")) return Dil::t("Sohbet bulunamadı.");
    if (k == L1("API_ID_INVALID")) return Dil::t("api_id / api_hash geçersiz. my.telegram.org'daki değerleri yeniden girin.");
    if (k == L1("API_ID_PUBLISHED_FLOOD")) return Dil::t("Bu api_id herkese açık yayınlandığı için Telegram tarafından kısıtlanmış. Kendi api_id'nizi girin.");
    if (k == L1("MESSAGE_NOT_MODIFIED")) return Dil::t("Mesaj değişmedi.");
    if (k == L1("MESSAGE_EDIT_TIME_EXPIRED")) return Dil::t("Bu mesajı düzenleme süresi doldu.");
    if (k == L1("MESSAGE_DELETE_FORBIDDEN")) return Dil::t("Bu mesajı silme izniniz yok.");
    if (k == L1("FILE_REFERENCE_EXPIRED")) return Dil::t("Dosyanın süresi doldu; sohbeti yeniden açın.");
    if (k == L1("CHAT_SEND_MEDIA_FORBIDDEN")) return Dil::t("Bu sohbete medya gönderilemiyor.");
    if (k == L1("REACTION_INVALID")) return Dil::t("Bu sohbette bu ifade kullanılamıyor.");
    return Dil::t("Telegram hatası: %1").arg(k);
}

// ======================================================================= onbellek ve donusumler
void Telegram::kisileriAl(const QVariantList &users)
{
    for (int i = 0; i < users.size(); ++i) {
        QVariantMap u = users[i].toMap();
        if (u.value(K("_")).toString() != L1("user")) continue;
        const qint64 id = uzun(u.value(K("id")));
        // "min" kisilerde access_hash eksik olabilir: eldekini koru
        if (!u.contains(K("access_hash")) && m_kisiler.contains(id))
            u.insert(K("access_hash"), m_kisiler.value(id).value(K("access_hash")));
        if (!u.contains(K("status")) && m_kisiler.contains(id))
            u.insert(K("status"), m_kisiler.value(id).value(K("status")));
        m_kisiler.insert(id, u);
    }
}

void Telegram::sohbetleriAl(const QVariantList &chats)
{
    for (int i = 0; i < chats.size(); ++i) {
        QVariantMap c = chats[i].toMap();
        const QString t = c.value(K("_")).toString();
        if (t != L1("chat") && t != L1("channel") && t != L1("chatForbidden") && t != L1("channelForbidden")) continue;
        const qint64 id = uzun(c.value(K("id")));
        if (!c.contains(K("access_hash")) && m_gruplar.contains(id))
            c.insert(K("access_hash"), m_gruplar.value(id).value(K("access_hash")));
        m_gruplar.insert(id, c);
    }
}

QString Telegram::anahtar(const QVariant &peer) const
{
    const QVariantMap p = peer.toMap();
    const QString t = p.value(K("_")).toString();
    if (t == L1("peerUser")) return L1("u") + QString::number(uzun(p.value(K("user_id"))));
    if (t == L1("peerChat")) return L1("c") + QString::number(uzun(p.value(K("chat_id"))));
    if (t == L1("peerChannel")) return L1("k") + QString::number(uzun(p.value(K("channel_id"))));
    return QString();
}

QVariantMap Telegram::girdiPeer(const QString &sohbet) const
{
    if (sohbet.isEmpty()) return harita("inputPeerEmpty");
    const qint64 id = sohbet.mid(1).toLongLong();
    const QChar t = sohbet.at(0);
    if (t == QLatin1Char('u')) {
        if (id == m_benId) return harita("inputPeerSelf");
        QVariantMap p = harita("inputPeerUser");
        p.insert(K("user_id"), id);
        p.insert(K("access_hash"), m_kisiler.value(id).value(K("access_hash")).toLongLong());
        return p;
    }
    if (t == QLatin1Char('c')) {
        QVariantMap p = harita("inputPeerChat");
        p.insert(K("chat_id"), id);
        return p;
    }
    QVariantMap p = harita("inputPeerChannel");
    p.insert(K("channel_id"), id);
    p.insert(K("access_hash"), m_gruplar.value(id).value(K("access_hash")).toLongLong());
    return p;
}

QVariantMap Telegram::girdiKanal(const QString &sohbet) const
{
    QVariantMap ch = harita("inputChannel");
    const qint64 id = sohbet.mid(1).toLongLong();
    ch.insert(K("channel_id"), id);
    ch.insert(K("access_hash"), m_gruplar.value(id).value(K("access_hash")).toLongLong());
    return ch;
}

QString Telegram::sohbetAdi(const QString &sohbet) const
{
    if (sohbet.isEmpty()) return QString();
    const qint64 id = sohbet.mid(1).toLongLong();
    if (sohbet.at(0) == QLatin1Char('u')) {
        if (id == m_benId) return Dil::t("Kayıtlı Mesajlar");
        const QVariantMap u = m_kisiler.value(id);
        if (u.isEmpty()) return Dil::t("Kişi");
        if (u.value(K("deleted")).toBool()) return Dil::t("Silinmiş hesap");
        const QString ad = (u.value(K("first_name")).toString() + L1(" ") + u.value(K("last_name")).toString()).trimmed();
        return ad.isEmpty() ? L1("+") + u.value(K("phone")).toString() : ad;
    }
    const QString t = m_gruplar.value(id).value(K("title")).toString();
    return t.isEmpty() ? Dil::t("Grup") : t;
}

static QString zamanMetni(uint t)
{
    const QDateTime d = QDateTime::fromTime_t(t);
    const QDate bugun = QDate::currentDate();
    if (d.date() == bugun) return Dil::t("bugün %1").arg(d.toString(L1("hh:mm")));
    if (d.date() == bugun.addDays(-1)) return Dil::t("dün %1").arg(d.toString(L1("hh:mm")));
    return d.toString(L1("dd.MM.yy hh:mm"));
}

QString Telegram::kisiDurumu(const QString &sohbet) const
{
    if (sohbet.isEmpty()) return QString();
    const qint64 id = sohbet.mid(1).toLongLong();
    if (sohbet.at(0) != QLatin1Char('u')) {
        const QVariantMap c = m_gruplar.value(id);
        const int n = c.value(K("participants_count")).toInt();
        if (sohbet.at(0) == QLatin1Char('k') && !c.value(K("megagroup")).toBool()) return n > 0 ? Dil::t("kanal, %1 abone").arg(n) : Dil::t("kanal");
        return n > 0 ? Dil::t("grup, %1 üye").arg(n) : Dil::t("grup");
    }
    if (id == m_benId) return QString();
    const QVariantMap u = m_kisiler.value(id);
    if (u.value(K("bot")).toBool()) return Dil::t("bot");
    const QVariantMap s = u.value(K("status")).toMap();
    const QString t = s.value(K("_")).toString();
    if (t == L1("userStatusOnline")) {
        if (s.value(K("expires")).toUInt() > QDateTime::currentDateTime().toTime_t()) return Dil::t("çevrimiçi");
        return Dil::t("son görülme %1").arg(zamanMetni(s.value(K("expires")).toUInt()));
    }
    if (t == L1("userStatusOffline")) return Dil::t("son görülme %1").arg(zamanMetni(s.value(K("was_online")).toUInt()));
    if (t == L1("userStatusRecently")) return Dil::t("yakınlarda görüldü");
    if (t == L1("userStatusLastWeek")) return Dil::t("bu hafta görüldü");
    if (t == L1("userStatusLastMonth")) return Dil::t("bu ay görüldü");
    return QString();
}

QVariantMap Telegram::kisiOge(qint64 id) const
{
    const QVariantMap u = m_kisiler.value(id);
    QVariantMap o;
    o.insert(K("id"), L1("u") + QString::number(id));
    o.insert(K("ad"), sohbetAdi(L1("u") + QString::number(id)));
    QString alt;
    if (!u.value(K("username")).toString().isEmpty()) alt = L1("@") + u.value(K("username")).toString();
    else if (!u.value(K("phone")).toString().isEmpty()) alt = L1("+") + u.value(K("phone")).toString();
    const QString d = kisiDurumu(L1("u") + QString::number(id));
    if (!d.isEmpty()) alt = alt.isEmpty() ? d : alt + L1("  ·  ") + d;
    o.insert(K("alt"), alt);
    o.insert(K("tur"), u.value(K("bot")).toBool() ? L1("bot") : L1("kisi"));
    return o;
}

QVariantMap Telegram::sohbetOge(const QString &key) const
{
    QVariantMap o;
    o.insert(K("id"), key);
    o.insert(K("ad"), sohbetAdi(key));
    if (key.isEmpty()) return o;
    if (key.at(0) == QLatin1Char('u')) {
        o.insert(K("tur"), L1("kisi"));
        const QString un = m_kisiler.value(key.mid(1).toLongLong()).value(K("username")).toString();
        o.insert(K("alt"), un.isEmpty() ? QString() : L1("@") + un);
        return o;
    }
    const QVariantMap c = m_gruplar.value(key.mid(1).toLongLong());
    o.insert(K("tur"), key.at(0) == QLatin1Char('c') || c.value(K("megagroup")).toBool() ? L1("grup") : L1("kanal"));
    const QString un = c.value(K("username")).toString();
    o.insert(K("alt"), un.isEmpty() ? kisiDurumu(key) : L1("@") + un);
    return o;
}

static QString servisMetni(const QVariantMap &action, const QString &kim)
{
    const QString t = action.value(K("_")).toString();
    if (t == L1("messageActionChatAddUser")) return Dil::t("%1 gruba ekledi").arg(kim);
    if (t == L1("messageActionChatJoinedByLink") || t == L1("messageActionChatJoinedByRequest")) return Dil::t("%1 gruba katıldı").arg(kim);
    if (t == L1("messageActionChatDeleteUser")) return Dil::t("%1 gruptan ayrıldı").arg(kim);
    if (t == L1("messageActionChatCreate") || t == L1("messageActionChannelCreate")) return Dil::t("%1 grubu oluşturdu").arg(kim);
    if (t == L1("messageActionChatEditTitle")) return Dil::t("Grup adı: %1").arg(action.value(K("title")).toString());
    if (t == L1("messageActionChatEditPhoto")) return Dil::t("Grup fotoğrafı değişti");
    if (t == L1("messageActionPinMessage")) return Dil::t("%1 bir mesajı sabitledi").arg(kim);
    if (t == L1("messageActionPhoneCall")) return Dil::t("Sesli arama");
    if (t == L1("messageActionContactSignUp")) return Dil::t("%1 Telegram'a katıldı").arg(kim);
    if (t == L1("messageActionHistoryClear")) return Dil::t("Geçmiş temizlendi");
    return Dil::t("Sohbet olayı");
}

// Fotograf boyutlari: s(100) m(320) x(800) y(1280) w(2560). Siradaki ilk mevcut olan secilir.
static QVariantMap boyutSec(const QVariantList &sizes, const char *sira)
{
    for (const char *c = sira; *c; ++c) {
        for (int i = 0; i < sizes.size(); ++i) {
            const QVariantMap s = sizes[i].toMap();
            const QString t = s.value(K("_")).toString();
            if (t != L1("photoSize") && t != L1("photoSizeProgressive")) continue;
            if (s.value(K("type")).toString() == QString(QLatin1Char(*c))) return s;
        }
    }
    return QVariantMap();
}

static qint64 boyutBayt(const QVariantMap &s)
{
    if (s.value(K("_")).toString() == L1("photoSizeProgressive")) {
        const QVariantList l = s.value(K("sizes")).toList();
        return l.isEmpty() ? 0 : l.last().toLongLong();
    }
    return s.value(K("size")).toLongLong();
}

void Telegram::medyaKaydiEkle(const QString &anahtar, int dc, const QVariantMap &konum, qint64 boyut, const QString &hedef, bool ses) const
{
    QVariantMap k;
    k.insert(K("dc"), dc);
    k.insert(K("konum"), konum);
    k.insert(K("boyut"), boyut);
    k.insert(K("hedef"), hedef);
    k.insert(K("ses"), ses);
    m_medya.insert(anahtar, k);
}

QVariantMap Telegram::mesajHarita(const QVariantMap &m) const
{
    QVariantMap h;
    if (m.isEmpty()) return h;
    const QString t = m.value(K("_")).toString();
    const QString sohbet = anahtar(m.value(K("peer_id")));
    QString gonderen = m.contains(K("from_id")) ? anahtar(m.value(K("from_id"))) : sohbet;
    if (m.value(K("out")).toBool() && sohbet.startsWith(QLatin1Char('u'))) gonderen = L1("u") + QString::number(m_benId);
    h.insert(K("id"), QString::number(m.value(K("id")).toInt()));
    h.insert(K("zaman"), double(m.value(K("date")).toUInt()) * 1000.0);
    h.insert(K("benden"), m.value(K("out")).toBool());
    h.insert(K("gonderenId"), gonderen);
    h.insert(K("gonderen"), sohbetAdi(gonderen));
    h.insert(K("metin"), m.value(K("message")).toString());
    h.insert(K("tur"), L1("metin"));
    if (m.contains(K("edit_date")) && !m.value(K("edit_hide")).toBool()) h.insert(K("duzenlendi"), true);
    const QVariantMap fwd = m.value(K("fwd_from")).toMap();
    if (!fwd.isEmpty()) {
        QString kim = fwd.value(K("from_name")).toString();
        if (kim.isEmpty() && fwd.contains(K("from_id"))) kim = sohbetAdi(anahtar(fwd.value(K("from_id"))));
        h.insert(K("iletildi"), true);
        h.insert(K("iletenAd"), kim);
    }
    const QVariantMap yanit = m.value(K("reply_to")).toMap();
    if (yanit.contains(K("reply_to_msg_id"))) h.insert(K("yanitId"), QString::number(yanit.value(K("reply_to_msg_id")).toInt()));
    // ifadeler (tepkiler)
    const QVariantList tl = m.value(K("reactions")).toMap().value(K("results")).toList();
    if (!tl.isEmpty()) {
        QStringList parca;
        QString benim;
        for (int i = 0; i < tl.size() && i < 4; ++i) {
            const QVariantMap rc = tl[i].toMap();
            const QString e = rc.value(K("reaction")).toMap().value(K("emoticon")).toString();
            if (e.isEmpty()) continue;
            const int n = rc.value(K("count")).toInt();
            parca << (n > 1 ? e + L1(" ") + QString::number(n) : e);
            if (rc.contains(K("chosen_order"))) benim = e;
        }
        h.insert(K("tepki"), parca.join(L1("  ")));
        h.insert(K("benimTepki"), benim);
    }
    if (t == L1("messageService")) {
        h.insert(K("tur"), L1("servis"));
        h.insert(K("metin"), servisMetni(m.value(K("action")).toMap(), sohbetAdi(gonderen)));
        return h;
    }
    const QVariantMap md = m.value(K("media")).toMap();
    const QString mt = md.value(K("_")).toString();
    if (mt.isEmpty() || mt == L1("messageMediaEmpty") || mt == L1("messageMediaWebPage")) {
        m_ozet.insert(sohbet + L1(":") + h.value(K("id")).toString(), h);
        if (m_ozet.size() > 800) m_ozet.clear();   // yalniz yanit alintilari icin; tam mesaj haritalari bellek yer
        return h;
    }
    if (mt == L1("messageMediaPhoto")) {
        h.insert(K("tur"), L1("foto"));
        const QVariantMap ph = md.value(K("photo")).toMap();
        if (ph.value(K("_")).toString() == L1("photo")) {
            const QString pid = QString::number(ph.value(K("id")).toLongLong());
            const QVariantList sizes = ph.value(K("sizes")).toList();
            const int dc = ph.value(K("dc_id")).toInt();
            const QVariantMap kucuk = boyutSec(sizes, "mxsy");
            const QVariantMap buyuk = boyutSec(sizes, "yxwm");
            QVariantMap konum = harita("inputPhotoFileLocation");
            konum.insert(K("id"), ph.value(K("id")));
            konum.insert(K("access_hash"), ph.value(K("access_hash")));
            konum.insert(K("file_reference"), ph.value(K("file_reference")));
            if (!kucuk.isEmpty()) {
                konum.insert(K("thumb_size"), kucuk.value(K("type")));
                medyaKaydiEkle(L1("p") + pid, dc, konum, boyutBayt(kucuk), medyaKlasoru() + L1("/p") + pid + L1(".jpg"), false);
                h.insert(K("medya"), L1("p") + pid);
                h.insert(K("en"), kucuk.value(K("w")).toInt());
                h.insert(K("boy"), kucuk.value(K("h")).toInt());
            }
            if (!buyuk.isEmpty()) {
                konum.insert(K("thumb_size"), buyuk.value(K("type")));
                medyaKaydiEkle(L1("P") + pid, dc, konum, boyutBayt(buyuk), medyaKlasoru() + L1("/P") + pid + L1(".jpg"), false);
                h.insert(K("buyukMedya"), L1("P") + pid);
            }
        }
    } else if (mt == L1("messageMediaGeo") || mt == L1("messageMediaGeoLive") || mt == L1("messageMediaVenue")) {
        const QVariantMap g = md.value(K("geo")).toMap();
        h.insert(K("tur"), L1("konum"));
        h.insert(K("lat"), g.value(K("lat")).toDouble());
        h.insert(K("lng"), g.value(K("long")).toDouble());
        h.insert(K("konumAd"), md.value(K("title")).toString());
        h.insert(K("adres"), md.value(K("address")).toString());
    } else if (mt == L1("messageMediaContact")) {
        h.insert(K("tur"), L1("kisi"));
        h.insert(K("dosyaAdi"), (md.value(K("first_name")).toString() + L1(" ") + md.value(K("last_name")).toString()).trimmed()
                 + L1(" +") + md.value(K("phone_number")).toString());
    } else if (mt == L1("messageMediaPoll")) {
        h.insert(K("tur"), L1("anket"));
        h.insert(K("dosyaAdi"), md.value(K("poll")).toMap().value(K("question")).toMap().value(K("text")).toString());
    } else if (mt == L1("messageMediaDice")) {
        h.insert(K("tur"), L1("zar"));
        h.insert(K("dosyaAdi"), md.value(K("emoticon")).toString() + L1(" ") + QString::number(md.value(K("value")).toInt()));
    } else if (mt == L1("messageMediaDocument")) {
        const QVariantMap doc = md.value(K("document")).toMap();
        h.insert(K("tur"), L1("dosya"));
        h.insert(K("boyut"), double(doc.value(K("size")).toLongLong()));
        const QString mime = doc.value(K("mime_type")).toString();
        h.insert(K("mime"), mime);
        const QVariantList at = doc.value(K("attributes")).toList();
        for (int i = 0; i < at.size(); ++i) {
            const QVariantMap a = at[i].toMap();
            const QString aa = a.value(K("_")).toString();
            if (aa == L1("documentAttributeAudio")) {
                h.insert(K("tur"), a.value(K("voice")).toBool() ? L1("ses") : L1("muzik"));
                h.insert(K("sure"), a.value(K("duration")).toInt());
                if (!a.value(K("voice")).toBool() && !a.value(K("title")).toString().isEmpty())
                    h.insert(K("dosyaAdi"), (a.value(K("performer")).toString() + L1(" - ") + a.value(K("title")).toString()));
            } else if (aa == L1("documentAttributeVideo")) {
                if (h.value(K("tur")).toString() == L1("dosya")) h.insert(K("tur"), a.value(K("round_message")).toBool() ? L1("yuvarlak") : L1("video"));
                h.insert(K("sure"), int(a.value(K("duration")).toDouble()));
            } else if (aa == L1("documentAttributeSticker")) {
                h.insert(K("tur"), L1("cikartma"));
                h.insert(K("dosyaAdi"), a.value(K("alt")).toString());
            } else if (aa == L1("documentAttributeAnimated")) {
                h.insert(K("tur"), L1("gif"));
            } else if (aa == L1("documentAttributeFilename")) {
                h.insert(K("dosyaAdi2"), a.value(K("file_name")).toString());
                if (!h.contains(K("dosyaAdi"))) h.insert(K("dosyaAdi"), a.value(K("file_name")).toString());
            }
        }
        if (doc.value(K("_")).toString() == L1("document")) {
            const QString did = QString::number(doc.value(K("id")).toLongLong());
            const int dc = doc.value(K("dc_id")).toInt();
            QVariantMap konum = harita("inputDocumentFileLocation");
            konum.insert(K("id"), doc.value(K("id")));
            konum.insert(K("access_hash"), doc.value(K("access_hash")));
            konum.insert(K("file_reference"), doc.value(K("file_reference")));
            konum.insert(K("thumb_size"), QString());
            const QString tur = h.value(K("tur")).toString();
            QString hedef;
            if (tur == L1("ses")) {
                hedef = medyaKlasoru() + L1("/d") + did + L1(".ogg");
            } else {
                QString ad = temizAd(h.value(K("dosyaAdi2")).toString());
                if (ad.isEmpty()) ad = L1("BelleGram_") + did + mimeUzanti(mime);
                hedef = indirmeKlasoru() + L1("/") + ad;
            }
            if (tur != L1("cikartma")) {
                medyaKaydiEkle(L1("d") + did, dc, konum, doc.value(K("size")).toLongLong(), hedef, tur == L1("ses"));
                h.insert(K("medya"), L1("d") + did);
            }
            // video / gif onizlemesi
            const QVariantMap kucuk = boyutSec(doc.value(K("thumbs")).toList(), "ms");
            if (!kucuk.isEmpty() && (tur == L1("video") || tur == L1("gif") || tur == L1("yuvarlak") || mime.startsWith(L1("image/")))) {
                konum.insert(K("thumb_size"), kucuk.value(K("type")));
                medyaKaydiEkle(L1("t") + did, dc, konum, boyutBayt(kucuk), medyaKlasoru() + L1("/t") + did + L1(".jpg"), false);
                h.insert(K("onizleme"), L1("t") + did);
                h.insert(K("en"), kucuk.value(K("w")).toInt());
                h.insert(K("boy"), kucuk.value(K("h")).toInt());
            }
        }
    } else {
        h.insert(K("tur"), L1("diger"));
    }
    m_ozet.insert(sohbet + L1(":") + h.value(K("id")).toString(), h);
    if (m_ozet.size() > 800) m_ozet.clear();
    return h;
}

void Telegram::yanitDoldur(QVariantMap &h, const QString &sohbet) const
{
    const QString y = h.value(K("yanitId")).toString();
    if (y.isEmpty()) return;
    const QVariantMap o = m_ozet.value(sohbet + L1(":") + y);
    if (o.isEmpty()) { h.insert(K("yanitAd"), QString()); h.insert(K("yanitOzet"), Dil::t("Yanıt")); return; }
    h.insert(K("yanitAd"), o.value(K("benden")).toBool() ? Dil::t("Siz") : o.value(K("gonderen")).toString());
    h.insert(K("yanitOzet"), mesajOzet(o));
}

QString Telegram::mesajOzet(const QVariantMap &h) const
{
    const QString metin = h.value(K("metin")).toString();
    const QString t = h.value(K("tur")).toString();
    QString etiket;
    if (t == L1("foto")) etiket = Dil::t("Fotoğraf");
    else if (t == L1("ses")) etiket = Dil::t("Sesli mesaj");
    else if (t == L1("video")) etiket = Dil::t("Video");
    else if (t == L1("yuvarlak")) etiket = Dil::t("Görüntülü mesaj");
    else if (t == L1("gif")) etiket = Dil::t("GIF");
    else if (t == L1("muzik")) etiket = Dil::t("Müzik");
    else if (t == L1("konum")) etiket = Dil::t("Konum");
    else if (t == L1("kisi")) etiket = Dil::t("Kişi");
    else if (t == L1("anket")) etiket = Dil::t("Anket");
    else if (t == L1("cikartma")) etiket = h.value(K("dosyaAdi")).toString() + L1(" ") + Dil::t("Çıkartma");
    else if (t == L1("dosya")) etiket = h.value(K("dosyaAdi")).toString().isEmpty() ? Dil::t("Dosya") : h.value(K("dosyaAdi")).toString();
    if (t == L1("servis")) return metin;
    if (etiket.isEmpty()) return metin;
    return metin.isEmpty() ? L1("[") + etiket + L1("]") : L1("[") + etiket + L1("] ") + metin;
}

// ======================================================================= sohbet islemleri
void Telegram::sohbetleriYukle(bool daha)
{
    QVariantMap p;
    p.insert(K("offset_date"), daha ? m_sonTarih : 0);
    p.insert(K("offset_id"), daha ? m_sonMesaj : 0);
    p.insert(K("offset_peer"), daha && !m_sonPeer.isEmpty() ? m_sonPeer : harita("inputPeerEmpty"));
    p.insert(K("limit"), 30);
    p.insert(K("hash"), qlonglong(0));
    cagir(L1("messages.getDialogs"), p, IsSohbet, QString(), 0, daha);
}

void Telegram::sessizAyarla(const QString &sohbet, bool sessiz)
{
    QVariantMap np = harita("inputNotifyPeer");
    np.insert(K("peer"), girdiPeer(sohbet));
    QVariantMap ay = harita("inputPeerNotifySettings");
    ay.insert(K("mute_until"), sessiz ? 0x7FFFFFFF : 0);
    QVariantMap p;
    p.insert(K("peer"), np);
    p.insert(K("settings"), ay);
    cagir(L1("account.updateNotifySettings"), p, IsBos, sohbet);
    emit sohbetOzellik(sohbet, L1("sessiz"), sessiz);
    emit bilgi(sessiz ? Dil::t("Sohbet sessize alındı") : Dil::t("Sohbetin sesi açıldı"));
}

void Telegram::sabitle(const QString &sohbet, bool sabit)
{
    QVariantMap dp = harita("inputDialogPeer");
    dp.insert(K("peer"), girdiPeer(sohbet));
    QVariantMap p;
    p.insert(K("pinned"), sabit);
    p.insert(K("peer"), dp);
    cagir(L1("messages.toggleDialogPin"), p, IsBos, sohbet);
    emit sohbetOzellik(sohbet, L1("sabit"), sabit);
}

void Telegram::sohbetSil(const QString &sohbet)
{
    if (sohbet.isEmpty()) return;
    if (sohbet.at(0) == QLatin1Char('k')) {
        QVariantMap p;
        p.insert(K("channel"), girdiKanal(sohbet));
        cagir(L1("channels.leaveChannel"), p, IsGuncelleme, sohbet);
    } else {
        QVariantMap p;
        p.insert(K("peer"), girdiPeer(sohbet));
        p.insert(K("max_id"), 0);
        cagir(L1("messages.deleteHistory"), p, IsBos, sohbet);
    }
    emit sohbetSilindi(sohbet);
}

// ======================================================================= mesaj islemleri
void Telegram::mesajlariYukle(const QString &sohbet, bool eskiler)
{
    QVariantMap p;
    p.insert(K("peer"), girdiPeer(sohbet));
    p.insert(K("offset_id"), eskiler ? m_enEski.value(sohbet) : 0);
    p.insert(K("offset_date"), 0);
    p.insert(K("add_offset"), 0);
    p.insert(K("limit"), 30);
    p.insert(K("max_id"), 0);
    p.insert(K("min_id"), 0);
    p.insert(K("hash"), qlonglong(0));
    cagir(L1("messages.getHistory"), p, IsGecmis, sohbet, 0, eskiler);
}

QVariantMap Telegram::yanitGirdi(const QString &yanitId) const
{
    QVariantMap r = harita("inputReplyToMessage");
    r.insert(K("reply_to_msg_id"), yanitId.toInt());
    return r;
}

QVariantMap Telegram::geciciMesaj(const QString &sohbet, const QString &gecici, const QString &tur, const QString &yanitId) const
{
    QVariantMap h;
    h.insert(K("id"), gecici);
    h.insert(K("benden"), true);
    h.insert(K("zaman"), double(QDateTime::currentDateTime().toMSecsSinceEpoch()));
    h.insert(K("tur"), tur);
    h.insert(K("bekliyor"), true);
    if (!yanitId.isEmpty()) {
        h.insert(K("yanitId"), yanitId);
        yanitDoldur(h, sohbet);
    }
    return h;
}

void Telegram::mesajGonder(const QString &sohbet, const QString &metin, const QString &yanitId)
{
    const QString t = metin.trimmed();
    if (t.isEmpty() || sohbet.isEmpty()) return;
    const qint64 rastgele = rastgeleId();
    const QString gecici = L1("g") + QString::number(quint64(rastgele) % 1000000000ULL);
    m_rastgele.insert(rastgele, sohbet + L1("|") + gecici);
    QVariantMap p;
    p.insert(K("peer"), girdiPeer(sohbet));
    p.insert(K("message"), t);
    p.insert(K("random_id"), rastgele);
    if (!yanitId.isEmpty()) p.insert(K("reply_to"), yanitGirdi(yanitId));
    cagir(L1("messages.sendMessage"), p, IsGonder, sohbet, rastgele);
    QVariantMap h = geciciMesaj(sohbet, gecici, L1("metin"), yanitId);
    h.insert(K("metin"), t);
    emit yeniMesaj(sohbet, h);
}

void Telegram::mesajDuzenle(const QString &sohbet, const QString &id, const QString &metin)
{
    const QString t = metin.trimmed();
    if (t.isEmpty()) return;
    QVariantMap p;
    p.insert(K("peer"), girdiPeer(sohbet));
    p.insert(K("id"), id.toInt());
    p.insert(K("message"), t);
    cagir(L1("messages.editMessage"), p, IsGuncelleme, sohbet);
    QVariantMap h;
    h.insert(K("id"), id);
    h.insert(K("metin"), t);
    h.insert(K("duzenlendi"), true);
    emit mesajDuzenlendi(sohbet, h);
}

void Telegram::mesajSil(const QString &sohbet, const QString &id, bool herkesten)
{
    QVariantList ids;
    ids << id.toInt();
    QVariantMap p;
    p.insert(K("id"), ids);
    if (sohbet.startsWith(QLatin1Char('k'))) {
        p.insert(K("channel"), girdiKanal(sohbet));
        cagir(L1("channels.deleteMessages"), p, IsBos, sohbet);
    } else {
        p.insert(K("revoke"), herkesten);
        cagir(L1("messages.deleteMessages"), p, IsBos, sohbet);
    }
    QVariantList l;
    l << id;
    emit mesajlarSilindi(sohbet, l);
}

void Telegram::ilet(const QString &kaynak, const QString &id, const QString &hedef)
{
    QVariantList ids, rs;
    ids << id.toInt();
    rs << rastgeleId();
    QVariantMap p;
    p.insert(K("from_peer"), girdiPeer(kaynak));
    p.insert(K("id"), ids);
    p.insert(K("random_id"), rs);
    p.insert(K("to_peer"), girdiPeer(hedef));
    cagir(L1("messages.forwardMessages"), p, IsGuncelleme, hedef);
    emit bilgi(Dil::t("İletildi: %1").arg(sohbetAdi(hedef)));
}

void Telegram::tepkiGonder(const QString &sohbet, const QString &id, const QString &emoji)
{
    QVariantMap p;
    p.insert(K("peer"), girdiPeer(sohbet));
    p.insert(K("msg_id"), id.toInt());
    QVariantList r;
    if (!emoji.isEmpty()) {
        QVariantMap e = harita("reactionEmoji");
        e.insert(K("emoticon"), emoji);
        r << e;
    }
    p.insert(K("reaction"), r);
    cagir(L1("messages.sendReaction"), p, IsGuncelleme, sohbet);
}

void Telegram::okundu(const QString &sohbet)
{
    const int enYeni = m_enYeni.value(sohbet);
    if (!enYeni || sohbet.isEmpty()) return;
    if (sohbet.at(0) == QLatin1Char('k')) {
        QVariantMap p;
        p.insert(K("channel"), girdiKanal(sohbet));
        p.insert(K("max_id"), enYeni);
        cagir(L1("channels.readHistory"), p, IsOkundu, sohbet);
    } else {
        QVariantMap p;
        p.insert(K("peer"), girdiPeer(sohbet));
        p.insert(K("max_id"), enYeni);
        cagir(L1("messages.readHistory"), p, IsOkundu, sohbet);
    }
}

void Telegram::yaziyor(const QString &sohbet, bool ses)
{
    if (sohbet.isEmpty() || !m_bagli) return;
    QVariantMap p;
    p.insert(K("peer"), girdiPeer(sohbet));
    p.insert(K("action"), harita(ses ? "sendMessageRecordAudioAction" : "sendMessageTypingAction"));
    cagir(L1("messages.setTyping"), p, IsBos, sohbet);
}

void Telegram::kisileriYukle()
{
    QVariantMap p;
    p.insert(K("hash"), qlonglong(0));
    cagir(L1("contacts.getContacts"), p, IsKisiler);
}

void Telegram::kisiAra(const QString &sorgu)
{
    const QString q = sorgu.trimmed();
    if (q.size() < 2) { emit aramaSonucu(sorgu, QVariantList()); return; }
    QVariantMap p;
    p.insert(K("q"), q.startsWith(QLatin1Char('@')) ? q.mid(1) : q);
    p.insert(K("limit"), 30);
    const int id = cagir(L1("contacts.search"), p, IsAra);
    if (m_isler.contains(id)) m_isler[id].metin = sorgu;
}

// ======================================================================= gonderme (medya)
void Telegram::konumGonder(const QString &sohbet, double lat, double lng, const QString &ad, const QString &adres)
{
    const qint64 rastgele = rastgeleId();
    const QString gecici = L1("g") + QString::number(quint64(rastgele) % 1000000000ULL);
    m_rastgele.insert(rastgele, sohbet + L1("|") + gecici);
    QVariantMap nokta = harita("inputGeoPoint");
    nokta.insert(K("lat"), lat);
    nokta.insert(K("long"), lng);
    QVariantMap medya;
    if (!ad.isEmpty()) {
        medya = harita("inputMediaVenue");
        medya.insert(K("geo_point"), nokta);
        medya.insert(K("title"), ad);
        medya.insert(K("address"), adres);
        medya.insert(K("provider"), QString());
        medya.insert(K("venue_id"), QString());
        medya.insert(K("venue_type"), QString());
    } else {
        medya = harita("inputMediaGeoPoint");
        medya.insert(K("geo_point"), nokta);
    }
    QVariantMap p;
    p.insert(K("peer"), girdiPeer(sohbet));
    p.insert(K("media"), medya);
    p.insert(K("message"), QString());
    p.insert(K("random_id"), rastgele);
    cagir(L1("messages.sendMedia"), p, IsGonder, sohbet, rastgele);
    QVariantMap h = geciciMesaj(sohbet, gecici, L1("konum"), QString());
    h.insert(K("lat"), lat);
    h.insert(K("lng"), lng);
    h.insert(K("konumAd"), ad);
    h.insert(K("adres"), adres);
    emit yeniMesaj(sohbet, h);
}

void Telegram::fotoGonder(const QString &sohbet, const QString &yol, const QString &aciklama, const QString &yanitId)
{
    // Telefondaki 8 MP fotograflar 1280 piksele kucultulur (bellek ve veri tasarrufu)
    QImageReader r(yol);
    QSize s = r.size();
    if (s.isValid() && qMax(s.width(), s.height()) > 1280) {
        s.scale(1280, 1280, Qt::KeepAspectRatio);
        r.setScaledSize(s);
    }
    const QImage img = r.read();
    if (img.isNull()) { emit hataOldu(Dil::t("Fotoğraf okunamadı.")); return; }
    QByteArray veri;
    QBuffer b(&veri);
    b.open(QIODevice::WriteOnly);
    img.save(&b, "JPG", 85);
    b.close();
    Yukleme y;
    y.sohbet = sohbet;
    y.tur = L1("foto");
    y.ad = L1("foto.jpg");
    y.mime = L1("image/jpeg");
    y.veri = veri;
    y.aciklama = aciklama.trimmed();
    y.yanit = yanitId;
    y.gecici = yuklemeEkle(y);
    QVariantMap h = geciciMesaj(sohbet, y.gecici, L1("foto"), yanitId);
    h.insert(K("metin"), y.aciklama);
    h.insert(K("yerel"), yol);
    h.insert(K("en"), img.width());
    h.insert(K("boy"), img.height());
    emit yeniMesaj(sohbet, h);
}

void Telegram::dosyaGonder(const QString &sohbet, const QString &yol, const QString &yanitId)
{
    QFile f(yol);
    if (!f.open(QIODevice::ReadOnly)) { emit hataOldu(Dil::t("Dosya açılamadı.")); return; }
    if (f.size() > EN_BUYUK_DOSYA) { emit hataOldu(Dil::t("Dosya çok büyük (en fazla 2000 MB).")); return; }
    if (f.size() <= 0) { emit hataOldu(Dil::t("Dosya boş")); return; }
    Yukleme y;
    y.sohbet = sohbet;
    y.tur = L1("dosya");
    y.ad = QFileInfo(yol).fileName();
    y.mime = uzantiMime(yol);
    y.yol = yol;            // icerik parca parca okunur
    y.boyut = f.size();
    f.close();
    y.yanit = yanitId;
    y.gecici = yuklemeEkle(y);
    QVariantMap h = geciciMesaj(sohbet, y.gecici, L1("dosya"), yanitId);
    h.insert(K("dosyaAdi"), y.ad);
    h.insert(K("boyut"), double(y.boyut));
    emit yeniMesaj(sohbet, h);
}

void Telegram::sesGonder(const QString &sohbet, const QString &yol, int sure, const QString &dalgaHex, const QString &yanitId)
{
    QFile f(yol);
    if (!f.open(QIODevice::ReadOnly)) { emit hataOldu(Dil::t("Kayıt açılamadı.")); return; }
    Yukleme y;
    y.sohbet = sohbet;
    y.tur = L1("ses");
    y.ad = L1("ses.ogg");
    y.mime = L1("audio/ogg");
    y.veri = f.readAll();
    f.close();
    y.sure = sure;
    y.dalga = QByteArray::fromHex(dalgaHex.toLatin1());
    y.yanit = yanitId;
    y.gecici = yuklemeEkle(y);
    // kendi kaydimiz da balonda calinabilsin: cozulmus hali onbellege
    QVariantMap h = geciciMesaj(sohbet, y.gecici, L1("ses"), yanitId);
    h.insert(K("sure"), sure);
    const QByteArray pcm = OpusSes::coz(y.veri, 16000);
    if (!pcm.isEmpty()) {
        const QString wav = medyaKlasoru() + L1("/") + y.gecici + L1(".wav");
        QFile w(wav);
        if (w.open(QIODevice::WriteOnly)) { w.write(OpusSes::wav(pcm, 16000)); w.close(); h.insert(K("yerel"), wav); }
    }
    QFile::remove(yol);
    emit yeniMesaj(sohbet, h);
}

QString Telegram::yuklemeEkle(Yukleme y)
{
    y.rastgele = rastgeleId();
    y.gecici = L1("g") + QString::number(quint64(y.rastgele) % 1000000000ULL);
    m_rastgele.insert(y.rastgele, y.sohbet + L1("|") + y.gecici);
    m_yukKuyruk.append(y);
    yuklemeBaslat();
    return y.gecici;
}

void Telegram::yuklemeBaslat()
{
    if (m_yukAktif || m_yukKuyruk.isEmpty() || !m_bagli) return;
    m_yuk = m_yukKuyruk.takeFirst();
    m_yukAktif = true;
    m_yuk.dosyaId = rastgeleId();
    m_yuk.parca = 0;
    const qint64 n = m_yuk.yol.isEmpty() ? qint64(m_yuk.veri.size()) : m_yuk.boyut;
    m_yuk.buyuk = n > 10 * 1024 * 1024;
    m_yuk.parcaBoy = m_yuk.buyuk ? PARCA_YUK_BUYUK : PARCA_YUK;
    m_yuk.toplam = int((n + m_yuk.parcaBoy - 1) / m_yuk.parcaBoy);
    kayit(L1("yukleme: %1 bayt, %2 parca").arg(n).arg(m_yuk.toplam));
    parcaGonder();
}

void Telegram::parcaGonder()
{
    QByteArray parca;
    if (m_yuk.yol.isEmpty()) {
        parca = m_yuk.veri.mid(m_yuk.parca * m_yuk.parcaBoy, m_yuk.parcaBoy);
    } else {
        QFile f(m_yuk.yol);
        if (!f.open(QIODevice::ReadOnly) || !f.seek(qint64(m_yuk.parca) * m_yuk.parcaBoy)) { yuklemeBitti(Dil::t("Dosya açılamadı.")); return; }
        parca = f.read(m_yuk.parcaBoy);
        if (parca.isEmpty()) { yuklemeBitti(Dil::t("Dosya açılamadı.")); return; }
    }
    QVariantMap p;
    p.insert(K("file_id"), m_yuk.dosyaId);
    p.insert(K("file_part"), m_yuk.parca);
    if (m_yuk.buyuk) p.insert(K("file_total_parts"), m_yuk.toplam);
    p.insert(K("bytes"), parca);
    cagir(m_yuk.buyuk ? L1("upload.saveBigFilePart") : L1("upload.saveFilePart"), p, IsParca, m_yuk.sohbet, m_yuk.rastgele);
}

void Telegram::yuklemeBitti(const QString &hata)
{
    if (!hata.isEmpty()) {
        m_rastgele.remove(m_yuk.rastgele);
        emit mesajGitmedi(m_yuk.sohbet, m_yuk.gecici, hata);
    }
    m_yukAktif = false;
    m_yuk = Yukleme();
    yuklemeBaslat();
}

// ======================================================================= indirme (medya)
// Medya onbellegi (fotograf, sesli mesaj, kucuk resim): kart/E varsa orada, yoksa ozel klasorde (kutuphane/depo)
QString Telegram::medyaKlasoru() const { return Depo::yol(L1("BelleGram/.medya"), m_klasor + L1("/medya")); }

QString Telegram::indirmeKlasoru() const
{
#ifdef Q_OS_SYMBIAN
    const QString k = Depo::yol(L1("BelleGram"), L1("C:/Data/BelleGram/Dosyalar"));
#else
    const QString k = m_klasor + L1("/Dosyalar");
#endif
    return k;
}

QString Telegram::sonDosya(const QString &anahtar) const
{
    const QVariantMap k = m_medya.value(anahtar);
    QString h = k.value(K("hedef")).toString();
    if (k.value(K("ses")).toBool()) h = h.left(h.size() - 4) + L1(".wav");
    return h;
}

QString Telegram::medyaYolu(const QString &anahtar) const
{
    if (!m_medya.contains(anahtar)) return QString();
    const QString p = sonDosya(anahtar);
    return QFileInfo(p).size() > 0 ? p : QString();
}

QString Telegram::medyaKaydet(const QString &anahtar)
{
    const QString kaynak = medyaYolu(anahtar);
    if (kaynak.isEmpty()) return QString();
#ifdef Q_OS_SYMBIAN
    const QString k = Depo::yol(L1("Images/BelleGram"));
#else
    const QString k = m_klasor + L1("/Resimler");
#endif
    QDir().mkpath(k);
    const QString hedef = k + L1("/BelleGram_") + QDateTime::currentDateTime().toString(L1("yyyyMMdd_hhmmss")) + L1(".jpg");
    return QFile::copy(kaynak, hedef) ? hedef : QString();
}

void Telegram::medyaIndir(const QString &anahtar)
{
    if (!m_medya.contains(anahtar)) { emit medyaHata(anahtar, Dil::t("Dosya bilgisi yok")); return; }
    const QString hazir = medyaYolu(anahtar);
    if (!hazir.isEmpty()) { emit medyaHazir(anahtar, hazir); return; }
    if (m_indAktif && m_ind.anahtar == anahtar) return;
    for (int i = 0; i < m_indKuyruk.size(); ++i) {
        if (m_indKuyruk[i].anahtar == anahtar) {
            // en son istenen once (ekranda gorunen)
            m_indKuyruk.move(i, 0);
            return;
        }
    }
    const QVariantMap k = m_medya.value(anahtar);
    Indirme d;
    d.anahtar = anahtar;
    d.dc = k.value(K("dc")).toInt();
    d.konum = k.value(K("konum")).toMap();
    d.boyut = k.value(K("boyut")).toLongLong();
    d.hedef = k.value(K("hedef")).toString();
    d.ses = k.value(K("ses")).toBool();
    if (d.boyut > EN_BUYUK_DOSYA) { emit medyaHata(anahtar, Dil::t("Dosya çok büyük (en fazla 2000 MB).")); return; }
    m_indKuyruk.prepend(d);
    while (m_indKuyruk.size() > 60) m_indKuyruk.removeLast();
    indirmeBaslat();
}

void Telegram::indirmeBaslat()
{
    if (m_indAktif || m_indKuyruk.isEmpty() || !m_bagli) return;
    m_ind = m_indKuyruk.takeFirst();
    m_indAktif = true;
    m_ind.alinan = 0;
    m_ind.parcaBoy = m_ind.boyut > 10 * 1024 * 1024 ? PARCA_IND_BUYUK : PARCA_IND;
    if (m_ind.dc <= 0 || m_ind.dc == m_anaDc) { parcaIste(); return; }
    dcBaglanti(m_ind.dc);
    if (dcYetkili(m_ind.dc)) { parcaIste(); return; }
    kayit(L1("dc%1 icin yetki aktariliyor").arg(m_ind.dc));
    QVariantMap p;
    p.insert(K("dc_id"), m_ind.dc);
    cagir(L1("auth.exportAuthorization"), p, IsDisaAktar, QString(), m_ind.dc);
    m_indZaman.start();
}

void Telegram::parcaIste()
{
    QVariantMap p;
    p.insert(K("location"), m_ind.konum);
    p.insert(K("offset"), qlonglong(m_ind.alinan));
    p.insert(K("limit"), m_ind.parcaBoy);
    if (m_ind.dc <= 0 || m_ind.dc == m_anaDc) {
        const int id = cagir(L1("upload.getFile"), p, IsDosya, QString(), m_ind.alinan);
        if (m_isler.contains(id)) m_isler[id].metin = m_ind.anahtar;
    } else {
        const int id = dcCagir(m_ind.dc, L1("upload.getFile"), p, IsDosya, m_ind.alinan);
        const qint64 a = dcIsAnahtari(m_ind.dc, id);
        if (m_dcIsler.contains(a)) m_dcIsler[a].metin = m_ind.anahtar;
    }
    m_indZaman.start();
}

void Telegram::dosyaParcasi(const Is &is, const QVariantMap &m)
{
    if (!m_indAktif || is.metin != m_ind.anahtar || is.ek != m_ind.alinan) return;
    m_indZaman.stop();
    const QByteArray b = m.value(K("bytes")).toByteArray();
    // Parca dogrudan <hedef>.part dosyasina eklenir; ilk parcada dosya sifirlanir
    const QString parcaDosya = m_ind.hedef + L1(".part");
    if (!b.isEmpty()) {
        if (m_ind.alinan == 0) QDir().mkpath(QFileInfo(m_ind.hedef).absolutePath());
        QFile pf(parcaDosya);
        if (!pf.open(m_ind.alinan == 0 ? QIODevice::WriteOnly : (QIODevice::WriteOnly | QIODevice::Append)) || pf.write(b) != b.size()) {
            pf.close(); QFile::remove(parcaDosya);
            indirmeBitti(Dil::t("Dosya yazılamadı")); return;
        }
        pf.close();
        m_ind.alinan += b.size();
    }
    if (m_ind.boyut > 0) emit medyaIlerleme(m_ind.anahtar, int(qMin<qint64>(100, m_ind.alinan * 100 / m_ind.boyut)));
    if (b.size() == m_ind.parcaBoy && !(m_ind.boyut > 0 && m_ind.alinan >= m_ind.boyut) && m_ind.alinan < EN_BUYUK_DOSYA) {
        parcaIste();
        return;
    }
    if (m_ind.alinan == 0) { indirmeBitti(Dil::t("Dosya boş")); return; }
    QFile::remove(m_ind.hedef);
    if (!QFile::rename(parcaDosya, m_ind.hedef)) { QFile::remove(parcaDosya); indirmeBitti(Dil::t("Dosya yazılamadı")); return; }
    if (m_ind.ses) {
        QString h;
        QByteArray sesVeri;
        { QFile sf(m_ind.hedef); if (sf.open(QIODevice::ReadOnly)) sesVeri = sf.readAll(); }
        const QByteArray pcm = OpusSes::coz(sesVeri, 16000, &h);
        if (pcm.isEmpty()) { kayit(L1("ses cozulemedi: ") + h); indirmeBitti(Dil::t("Ses çözülemedi")); return; }
        QFile w(sonDosya(m_ind.anahtar));
        if (!w.open(QIODevice::WriteOnly)) { indirmeBitti(Dil::t("Dosya yazılamadı")); return; }
        w.write(OpusSes::wav(pcm, 16000));
        w.close();
    }
    indirmeBitti(QString());
}

void Telegram::indirmeBitti(const QString &hata)
{
    m_indZaman.stop();
    const QString a = m_ind.anahtar;
    if (!hata.isEmpty()) kayit(L1("indirme hatasi ") + a + L1(": ") + hata);
    m_indAktif = false;
    m_ind = Indirme();
    if (hata.isEmpty()) emit medyaHazir(a, sonDosya(a));
    else emit medyaHata(a, hata);
    indirmeBaslat();
}

void Telegram::indirmeZamanAsimi()
{
    if (!m_indAktif) return;
    if (++m_ind.deneme <= 2) {
        kayit(L1("indirme zaman asimi, yeniden: ") + m_ind.anahtar);
        if (m_ind.dc > 0 && m_ind.dc != m_anaDc && !dcYetkili(m_ind.dc)) {
            Indirme d = m_ind;
            m_indAktif = false;
            m_indKuyruk.prepend(d);
            indirmeBaslat();
            return;
        }
        parcaIste();
        return;
    }
    indirmeBitti(Dil::t("Zaman aşımı"));
}

// ======================================================================= medya veri merkezleri
Baglanti *Telegram::dcBaglanti(int dc)
{
    if (dc == m_anaDc) return m_b;
    if (m_dcB.contains(dc)) {
        Baglanti *b = m_dcB.value(dc);
        if (!b->hazir()) b->baglan();
        return b;
    }
    Baglanti *b = new Baglanti(dc, dcAdres(dc), 443, false, this);
    initAyarla(b);
    QSettings s(m_klasor + L1("/oturum.ini"), QSettings::IniFormat);
    const QByteArray a = QByteArray::fromBase64(s.value(L1("dc%1/anahtar").arg(dc)).toString().toLatin1());
    if (a.size() == 256) {
        b->anahtarAyarla(a, s.value(L1("dc%1/tuz").arg(dc)).toString().toLongLong());
        if (s.value(L1("dc%1/yetki").arg(dc)).toString() == QString::number(m_benId)) m_dcHazir.insert(dc);
    }
    connect(b, SIGNAL(anahtarHazir()), this, SLOT(dcAnahtar()));
    connect(b, SIGNAL(sonuc(int,QVariant)), this, SLOT(dcSonuc(int,QVariant)));
    connect(b, SIGNAL(hata(int,int,QString)), this, SLOT(dcHata(int,int,QString)));
    connect(b, SIGNAL(koptu(QString)), this, SLOT(dcKoptu(QString)));
    connect(b, SIGNAL(gunluk(QString)), this, SLOT(kayit(QString)));
    m_dcB.insert(dc, b);
    b->baglan();
    return b;
}

bool Telegram::dcYetkili(int dc) const { return m_dcHazir.contains(dc); }

int Telegram::dcCagir(int dc, const QString &yontem, const QVariantMap &p, int tur, qint64 ek)
{
    Baglanti *b = dcBaglanti(dc);
    if (!b) return 0;
    QByteArray sonucTuru;
    const QByteArray g = TlSema::tek().cagri(yontem, p, &sonucTuru);
    if (g.isEmpty()) { kayit(L1("serilestirilemedi: ") + yontem); return 0; }
    const int id = b->gonder(g, sonucTuru);
    Is is;
    is.tur = tur; is.ek = ek;
    m_dcIsler.insert(dcIsAnahtari(dc, id), is);
    return id;
}

void Telegram::dcAnahtar()
{
    Baglanti *b = qobject_cast<Baglanti *>(sender());
    if (!b) return;
    QSettings s(m_klasor + L1("/oturum.ini"), QSettings::IniFormat);
    const QString yeni = QString::fromLatin1(b->anahtar().toBase64());
    if (s.value(L1("dc%1/anahtar").arg(b->dc())).toString() != yeni) {
        s.setValue(L1("dc%1/anahtar").arg(b->dc()), yeni);
        s.remove(L1("dc%1/yetki").arg(b->dc()));
        m_dcHazir.remove(b->dc());
        kayit(L1("dc%1 medya anahtari uretildi").arg(b->dc()));
    }
    s.setValue(L1("dc%1/tuz").arg(b->dc()), QString::number(b->tuz()));
}

void Telegram::dcSonuc(int istek, const QVariant &v)
{
    Baglanti *b = qobject_cast<Baglanti *>(sender());
    if (!b) return;
    const qint64 a = dcIsAnahtari(b->dc(), istek);
    if (!m_dcIsler.contains(a)) return;
    dcIsSonucu(b->dc(), m_dcIsler.take(a), v);
}

void Telegram::dcIsSonucu(int dc, const Is &is, const QVariant &v)
{
    if (is.tur == IsIceAktar) {
        m_dcHazir.insert(dc);
        QSettings s(m_klasor + L1("/oturum.ini"), QSettings::IniFormat);
        s.setValue(L1("dc%1/yetki").arg(dc), QString::number(m_benId));
        kayit(L1("dc%1 yetkili").arg(dc));
        if (m_indAktif && m_ind.dc == dc) parcaIste();
    } else if (is.tur == IsDosya) {
        dosyaParcasi(is, v.toMap());
    }
}

void Telegram::dcHata(int istek, int kod, const QString &mesaj)
{
    Baglanti *b = qobject_cast<Baglanti *>(sender());
    if (!b) return;
    const int dc = b->dc();
    const Is is = m_dcIsler.take(dcIsAnahtari(dc, istek));
    kayit(L1("dc%1 hata %2 %3").arg(dc).arg(kod).arg(mesaj));
    if (!m_indAktif) return;
    if (kod == 401 && m_ind.deneme++ < 2) {
        // yetki yok/dusmus: yeniden aktar
        m_dcHazir.remove(dc);
        QSettings s(m_klasor + L1("/oturum.ini"), QSettings::IniFormat);
        s.remove(L1("dc%1/yetki").arg(dc));
        Indirme d = m_ind;
        d.alinan = 0;
        m_indAktif = false;
        m_indKuyruk.prepend(d);
        indirmeBaslat();
        return;
    }
    if (is.tur == IsDosya && is.metin != m_ind.anahtar) return;
    indirmeBitti(hataMetni(mesaj));
}

void Telegram::dcKoptu(const QString &neden)
{
    kayit(L1("medya baglantisi koptu: ") + neden);
    QTimer::singleShot(3000, this, SLOT(dcYeniden()));
}

void Telegram::dcYeniden()
{
    if (!m_indAktif) return;
    Baglanti *b = m_dcB.value(m_ind.dc);
    if (b && !b->hazir()) b->baglan();
}

// ======================================================================= guncellemeler
void Telegram::yeniMesajIsle(const QVariantMap &m)
{
    const QString sohbet = anahtar(m.value(K("peer_id")));
    if (sohbet.isEmpty()) return;
    QVariantMap h = mesajHarita(m);
    yanitDoldur(h, sohbet);
    const QString kim = sohbet + L1(":") + h.value(K("id")).toString();
    if (m_gorulen.contains(kim)) return;
    m_gorulen.insert(kim);
    const int id = h.value(K("id")).toInt();
    if (id > m_enYeni.value(sohbet)) m_enYeni.insert(sohbet, id);
    emit yeniMesaj(sohbet, h);
    emit yaziyorBilgi(sohbet, QString());
    QVariantMap o = sohbetOge(sohbet);
    o.insert(K("son"), mesajOzet(h));
    o.insert(K("zaman"), h.value(K("zaman")));
    o.insert(K("benden"), h.value(K("benden")));
    emit sohbetGuncellendi(o);
}

QString Telegram::yaziyorMetni(const QVariantMap &action) const
{
    const QString t = action.value(K("_")).toString();
    if (t == L1("sendMessageCancelAction")) return QString();
    if (t == L1("sendMessageTypingAction")) return Dil::t("yazıyor…");
    if (t == L1("sendMessageRecordAudioAction")) return Dil::t("ses kaydediyor…");
    if (t == L1("sendMessageUploadAudioAction")) return Dil::t("sesli mesaj gönderiyor…");
    if (t == L1("sendMessageUploadPhotoAction")) return Dil::t("fotoğraf gönderiyor…");
    if (t == L1("sendMessageUploadVideoAction") || t == L1("sendMessageRecordVideoAction")) return Dil::t("video gönderiyor…");
    if (t == L1("sendMessageUploadDocumentAction")) return Dil::t("dosya gönderiyor…");
    if (t == L1("sendMessageGeoLocationAction")) return Dil::t("konum seçiyor…");
    if (t == L1("sendMessageChooseStickerAction")) return Dil::t("çıkartma seçiyor…");
    return Dil::t("yazıyor…");
}

void Telegram::tekGuncelleme(const QVariantMap &u)
{
    const QString t = u.value(K("_")).toString();
    if (t == L1("updateNewMessage") || t == L1("updateNewChannelMessage")) {
        yeniMesajIsle(u.value(K("message")).toMap());
    } else if (t == L1("updateEditMessage") || t == L1("updateEditChannelMessage")) {
        const QVariantMap m = u.value(K("message")).toMap();
        const QString sohbet = anahtar(m.value(K("peer_id")));
        QVariantMap h = mesajHarita(m);
        yanitDoldur(h, sohbet);
        emit mesajDuzenlendi(sohbet, h);
    } else if (t == L1("updateDeleteMessages")) {
        QVariantList l;
        const QVariantList ids = u.value(K("messages")).toList();
        for (int i = 0; i < ids.size(); ++i) l << QString::number(ids[i].toInt());
        emit mesajlarSilindi(QString(), l);
    } else if (t == L1("updateDeleteChannelMessages")) {
        QVariantList l;
        const QVariantList ids = u.value(K("messages")).toList();
        for (int i = 0; i < ids.size(); ++i) l << QString::number(ids[i].toInt());
        emit mesajlarSilindi(L1("k") + QString::number(uzun(u.value(K("channel_id")))), l);
    } else if (t == L1("updateReadHistoryOutbox")) {
        const QString s = anahtar(u.value(K("peer")));
        m_okunanCikis.insert(s, u.value(K("max_id")).toInt());
        emit okunduBilgi(s, u.value(K("max_id")).toInt());
    } else if (t == L1("updateReadChannelOutbox")) {
        const QString s = L1("k") + QString::number(uzun(u.value(K("channel_id"))));
        m_okunanCikis.insert(s, u.value(K("max_id")).toInt());
        emit okunduBilgi(s, u.value(K("max_id")).toInt());
    } else if (t == L1("updateReadHistoryInbox")) {
        emit sohbetOzellik(anahtar(u.value(K("peer"))), L1("okunmamis"), u.value(K("still_unread_count")).toInt());
    } else if (t == L1("updateReadChannelInbox")) {
        emit sohbetOzellik(L1("k") + QString::number(uzun(u.value(K("channel_id")))), L1("okunmamis"), u.value(K("still_unread_count")).toInt());
    } else if (t == L1("updateUserTyping")) {
        emit yaziyorBilgi(L1("u") + QString::number(uzun(u.value(K("user_id")))), yaziyorMetni(u.value(K("action")).toMap()));
    } else if (t == L1("updateChatUserTyping") || t == L1("updateChannelUserTyping")) {
        const QString s = t == L1("updateChatUserTyping") ? L1("c") + QString::number(uzun(u.value(K("chat_id"))))
                                                          : L1("k") + QString::number(uzun(u.value(K("channel_id"))));
        const QString kim = sohbetAdi(anahtar(u.value(K("from_id")))).section(QLatin1Char(' '), 0, 0);
        const QString ne = yaziyorMetni(u.value(K("action")).toMap());
        emit yaziyorBilgi(s, ne.isEmpty() ? QString() : kim + L1(" ") + ne);
    } else if (t == L1("updateUserStatus")) {
        const qint64 id = uzun(u.value(K("user_id")));
        if (m_kisiler.contains(id)) {
            m_kisiler[id].insert(K("status"), u.value(K("status")));
            emit kisiDurumDegisti(L1("u") + QString::number(id));
        }
    } else if (t == L1("updateNotifySettings")) {
        const QVariantMap np = u.value(K("peer")).toMap();
        if (np.value(K("_")).toString() == L1("notifyPeer")) {
            const uint sus = u.value(K("notify_settings")).toMap().value(K("mute_until")).toUInt();
            emit sohbetOzellik(anahtar(np.value(K("peer"))), L1("sessiz"), sus > QDateTime::currentDateTime().toTime_t());
        }
    } else if (t == L1("updateDialogPinned")) {
        const QVariantMap dp = u.value(K("peer")).toMap();
        if (dp.value(K("_")).toString() == L1("dialogPeer"))
            emit sohbetOzellik(anahtar(dp.value(K("peer"))), L1("sabit"), u.value(K("pinned")).toBool());
    } else if (t == L1("updateMessageReactions")) {
        QVariantMap m;
        m.insert(K("_"), L1("message"));
        m.insert(K("peer_id"), u.value(K("peer")));
        m.insert(K("id"), u.value(K("msg_id")));
        m.insert(K("reactions"), u.value(K("reactions")));
        const QVariantList tl = u.value(K("reactions")).toMap().value(K("results")).toList();
        QStringList parca;
        QString benim;
        for (int i = 0; i < tl.size() && i < 4; ++i) {
            const QVariantMap rc = tl[i].toMap();
            const QString e = rc.value(K("reaction")).toMap().value(K("emoticon")).toString();
            if (e.isEmpty()) continue;
            const int n = rc.value(K("count")).toInt();
            parca << (n > 1 ? e + L1(" ") + QString::number(n) : e);
            if (rc.contains(K("chosen_order"))) benim = e;
        }
        emit tepkiDegisti(anahtar(u.value(K("peer"))), QString::number(u.value(K("msg_id")).toInt()), parca.join(L1("  ")), benim);
    }
}

void Telegram::guncelleme(const QVariant &v)
{
    const QVariantMap m = v.toMap();
    const QString t = m.value(K("_")).toString();
    if (t == L1("updates") || t == L1("updatesCombined")) {
        kisileriAl(m.value(K("users")).toList());
        sohbetleriAl(m.value(K("chats")).toList());
        const QVariantList ul = m.value(K("updates")).toList();
        for (int i = 0; i < ul.size(); ++i) tekGuncelleme(ul[i].toMap());
    } else if (t == L1("updateShort")) {
        tekGuncelleme(m.value(K("update")).toMap());
    } else if (t == L1("updateShortMessage") || t == L1("updateShortChatMessage")) {
        QVariantMap mm = m;
        mm.insert(K("_"), L1("message"));
        QVariantMap peer;
        if (t == L1("updateShortMessage")) {
            peer = harita("peerUser");
            peer.insert(K("user_id"), m.value(K("user_id")));
            if (!m.value(K("out")).toBool()) mm.insert(K("from_id"), peer);
        } else {
            peer = harita("peerChat");
            peer.insert(K("chat_id"), m.value(K("chat_id")));
            QVariantMap kimden = harita("peerUser");
            kimden.insert(K("user_id"), m.value(K("from_id")));
            mm.insert(K("from_id"), kimden);
        }
        mm.insert(K("peer_id"), peer);
        yeniMesajIsle(mm);
    } else if (t == L1("updatesTooLong")) {
        kayit(L1("updatesTooLong: sohbetler yenileniyor"));
        sohbetleriYukle(false);
    }
}
