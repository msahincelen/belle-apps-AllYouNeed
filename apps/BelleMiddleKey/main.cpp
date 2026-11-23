// BelleMiddleKey (eski OrtaTus + TusTest): istedigin tusa istedigin kisayolu atar.
//
// Iki parca:
//   BelleMiddleKeyServis.exe : Qt'siz arka plan servisi, telefon acilisinda baslar (servis/servis.cpp)
//   BelleMiddleKey.exe       : bu ayar uygulamasi; yalnizca acikken bellekte
//
// Kisayol ekleme: 1) eylem secilir  2) tusa bir kez bas-cek, bir kez basili tut  3) tetik secilir
// (bas-cek / pes pese 2-3 kez / basili tut). Test ekrani algilananlari gosterir, istenirse eylemi calistirir;
// ham tus kodlarini da gosterebilir (TusTest).

#include <QtGui/QApplication>
#include <QtGui/QWidget>
#include <QtGui/QStackedWidget>
#include <QtGui/QLabel>
#include <QtGui/QPushButton>
#include <QtGui/QCheckBox>
#include <QtGui/QListWidget>
#include <QtGui/QVBoxLayout>
#include <QtGui/QHBoxLayout>
#include <QtGui/QAction>
#include <QtGui/QSymbianEvent>
#include <QtCore/QTimer>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>
#include <QtCore/QList>
#include <QtCore/QPair>
#include "dil.h"

#include <e32base.h>
#include <e32keys.h>
#include <coemain.h>
#include <w32std.h>
#include <apgwgnam.h>
#include <apgcli.h>
#include "motor.h"
#include "eylem.h"

static const TUint32 KKameraUid = 0x101F857A;

// ---------------------------------------------------------------- servis denetimi
static TInt servisWg()
{
    TInt wg = 0;
    CApaWindowGroupName::FindByAppUid(TUid::Uid(KBmkServisUid), CCoeEnv::Static()->WsSession(), wg);
    return wg > 0 ? wg : 0;
}

static void servisGonder(TInt tur)
{
    const TInt wg = servisWg();
    if (!wg) return;
    TWsEvent e;
    e.SetType(tur);
    e.SetTimeNow();
    CCoeEnv::Static()->WsSession().SendEventToWindowGroup(wg, e);
}

static TInt servisBaslat()
{
    if (servisWg()) return KErrNone;
    RProcess p;
    TInt err = p.Create(_L("BelleMiddleKeyServis.exe"), KNullDesC);
    if (err == KErrNone) { p.Resume(); p.Close(); }
    return err;
}

// ---------------------------------------------------------------- adlar
static QString tusAdi(int scan, uint code)
{
    switch (scan) {
    case EStdKeyDevice6: return Dil::t("Orta tuş (ses tuşları arası)");
    case EStdKeyIncVolume: return Dil::t("Ses +");
    case EStdKeyDecVolume: return Dil::t("Ses -");
    case EStdKeyApplication0: return Dil::t("Menü tuşu");
    case EStdKeyYes: return Dil::t("Yeşil (arama) tuşu");
    case EStdKeyNo: return Dil::t("Kırmızı (kapatma) tuşu");
    case EStdKeyBackspace: return Dil::t("Silme tuşu");
    case EStdKeyEnter: return QString::fromLatin1("Enter");
    case EStdKeySpace: return Dil::t("Boşluk");
    case EStdKeyDevice3: return Dil::t("Yön tuşu ortası");
    case EStdKeyUpArrow: return Dil::t("Yukarı");
    case EStdKeyDownArrow: return Dil::t("Aşağı");
    case EStdKeyLeftArrow: return Dil::t("Sol");
    case EStdKeyRightArrow: return Dil::t("Sağ");
    }
    if (code >= 0x21 && code < 0xE000) return Dil::t("'%1' tuşu").arg(QChar(code).toUpper());
    return Dil::t("Tuş %1").arg(QString::fromLatin1("0x%1").arg(scan, 0, 16));
}

static QString uygulamaAdi(TUint32 uid)
{
    QString ad;
    RApaLsSession ls;
    if (ls.Connect() == KErrNone) {
        TApaAppInfo info;
        if (ls.GetAppInfo(info, TUid::Uid(uid)) == KErrNone)
            ad = QString::fromUtf16(info.iCaption.Ptr(), info.iCaption.Length());
        ls.Close();
    }
    if (uid == KKameraUid && ad.isEmpty()) ad = Dil::t("Kamera");
    return ad.isEmpty() ? QString::fromLatin1("0x%1").arg(uid, 0, 16) : ad;
}

static QString eylemAdi(int eylem, TUint32 param)
{
    switch (eylem) {
    case EEylemUygulama: return Dil::t("Aç: %1").arg(uygulamaAdi(param));
    case EEylemSessiz: return Dil::t("Sessiz ↔ önceki profil");
    case EEylemKapat: return Dil::t("Açık uygulamaları kapat");
    case EEylemMenu: return Dil::t("Ana menü (Menü tuşu)");
    case EEylemGorev: return Dil::t("Görev değiştirici");
    case EEylemSolSoftkey: return Dil::t("Sol Softkey (Seçenekler)");
    case EEylemSagSoftkey: return Dil::t("Sağ Softkey (Geri / Çıkış)");
    }
    return QString();
}

static QString tetikAdi(int tetik, int sayi)
{
    if (tetik == ETetikUzun) return Dil::t("basılı tut");
    if (tetik == ETetikCok) return Dil::t("%1 kez bas").arg(sayi);
    return Dil::t("bas-çek");
}

static bool yaziTusu(int scan)
{
    // klavye harf/rakam/isaret tuslari: yalnizca "basili tut" onerilir (kisa basis yazmaya devam eder)
    return scan < EStdKeyIncVolume && scan != EStdKeyApplication0;
}

// ---------------------------------------------------------------- uygulama
class Uygulama : public QApplication, public MMotorDinleyici
{
    Q_OBJECT
public:
    Uygulama(int &argc, char **argv);
    ~Uygulama();
    void kur();
    bool symbianEventFilter(const QSymbianEvent *ev);
    // MMotorDinleyici (test ekrani)
    void Tetiklendi(const TKisayol &k);
    void Eslesmedi(TInt scan, TUint code, TInt tetik, TInt sayi);

public slots:
    void anaSayfa();
    void yeniKisayol();
    void sil();
    void testBasla();
    void testBitir();
    void servisDegistir();
    void titresimDegisti(bool v);
    void eylemSecildi(QListWidgetItem *it);
    void uygulamaSecildi(QListWidgetItem *it);
    void tetikSecildi(QListWidgetItem *it);
    void kayitIptal();
    void geri();
    void durumYenile();

private:
    enum Kip { KNormal, KKayit1, KKayit2, KTest };
    QWidget *sayfa(QVBoxLayout *&lay);
    void kayitBasla();
    void kayitYakala();
    void kayitBirak();
    void listeYenile();
    void logla(const QString &s);
    bool kayitOlayi(int tur, const TKeyEvent &k);

    QWidget pencere;
    QStackedWidget *yigin;
    QWidget *anaS, *eylemS, *uygS, *kayitS, *tetikS, *testS;
    QLabel *durum, *kayitYazi, *kayitTus, *tetikUyari;
    QListWidget *liste, *eylemListe, *uygListe, *tetikListe, *testLog;
    QPushButton *servisBtn;
    QCheckBox *titresimK, *testCalistir, *testHam;

    CMotor *motor;
    Kip kip;
    TKisayol yeni;
    QList<QPair<TInt32, int> > kayitYakalama;
    QElapsedTimer basisSure;
    int kayitScan;
    uint kayitKod;
};

Uygulama::Uygulama(int &argc, char **argv)
    : QApplication(argc, argv), motor(0), kip(KNormal), kayitScan(-1), kayitKod(0)
{
}

Uygulama::~Uygulama()
{
    if (kip == KTest || kip == KKayit1 || kip == KKayit2) servisGonder(KBmkSurdur);
    kayitBirak();
    delete motor;
}

QWidget *Uygulama::sayfa(QVBoxLayout *&lay)
{
    QWidget *w = new QWidget;
    lay = new QVBoxLayout(w);
    lay->setContentsMargins(6, 4, 6, 4);
    lay->setSpacing(4);
    yigin->addWidget(w);
    return w;
}

void Uygulama::kur()
{
    pencere.setWindowTitle(QString::fromLatin1("BelleMiddleKey"));
    QVBoxLayout *kok = new QVBoxLayout(&pencere);
    kok->setContentsMargins(0, 0, 0, 0);
    yigin = new QStackedWidget;
    kok->addWidget(yigin);
    QVBoxLayout *lay;

    // --- ana sayfa
    anaS = sayfa(lay);
    durum = new QLabel;
    durum->setWordWrap(true);
    lay->addWidget(durum);
    liste = new QListWidget;
    lay->addWidget(liste, 1);
    QHBoxLayout *s1 = new QHBoxLayout;
    QPushButton *yeniB = new QPushButton(Dil::t("Yeni kısayol"));
    QPushButton *silB = new QPushButton(Dil::t("Sil"));
    QPushButton *testB = new QPushButton(Dil::t("Test et"));
    s1->addWidget(yeniB); s1->addWidget(silB); s1->addWidget(testB);
    lay->addLayout(s1);
    QHBoxLayout *s2 = new QHBoxLayout;
    titresimK = new QCheckBox(Dil::t("Titreşimle onayla"));
    servisBtn = new QPushButton;
    s2->addWidget(titresimK, 1); s2->addWidget(servisBtn);
    lay->addLayout(s2);
    connect(yeniB, SIGNAL(clicked()), this, SLOT(yeniKisayol()));
    connect(silB, SIGNAL(clicked()), this, SLOT(sil()));
    connect(testB, SIGNAL(clicked()), this, SLOT(testBasla()));
    connect(servisBtn, SIGNAL(clicked()), this, SLOT(servisDegistir()));
    connect(titresimK, SIGNAL(toggled(bool)), this, SLOT(titresimDegisti(bool)));

    // --- 1. eylem
    eylemS = sayfa(lay);
    lay->addWidget(new QLabel(Dil::t("<b>1/3</b> Tuşa ne yaptırmak istiyorsunuz?")));
    eylemListe = new QListWidget;
    struct { const char *ad; int eylem; TUint32 param; } eylemler[] = {
        { QT_TR_NOOP("Uygulama aç…"), EEylemUygulama, 0 },
        { QT_TR_NOOP("Kamera"), EEylemUygulama, KKameraUid },
        { QT_TR_NOOP("Sessiz ↔ önceki profil"), EEylemSessiz, 0 },
        { QT_TR_NOOP("Açık uygulamaları kapat"), EEylemKapat, 0 },
        { QT_TR_NOOP("Ana menü (Menü tuşu)"), EEylemMenu, 0 },
        { QT_TR_NOOP("Görev değiştirici"), EEylemGorev, 0 },
        { QT_TR_NOOP("Sol Softkey (Seçenekler)"), EEylemSolSoftkey, 0 },
        { QT_TR_NOOP("Sağ Softkey (Geri / Çıkış)"), EEylemSagSoftkey, 0 } };
    for (unsigned i = 0; i < sizeof(eylemler) / sizeof(eylemler[0]); ++i) {
        QListWidgetItem *it = new QListWidgetItem(Dil::t(eylemler[i].ad), eylemListe);
        it->setData(Qt::UserRole, eylemler[i].eylem);
        it->setData(Qt::UserRole + 1, (uint)eylemler[i].param);
    }
    lay->addWidget(eylemListe, 1);
    QPushButton *g1 = new QPushButton(Dil::t("Vazgeç"));
    lay->addWidget(g1);
    connect(g1, SIGNAL(clicked()), this, SLOT(anaSayfa()));
    connect(eylemListe, SIGNAL(itemActivated(QListWidgetItem*)), this, SLOT(eylemSecildi(QListWidgetItem*)));

    // --- 1b. uygulama listesi
    uygS = sayfa(lay);
    lay->addWidget(new QLabel(Dil::t("Hangi uygulama açılsın?")));
    uygListe = new QListWidget;
    lay->addWidget(uygListe, 1);
    QPushButton *g2 = new QPushButton(Dil::t("Geri"));
    lay->addWidget(g2);
    connect(g2, SIGNAL(clicked()), this, SLOT(geri()));
    connect(uygListe, SIGNAL(itemActivated(QListWidgetItem*)), this, SLOT(uygulamaSecildi(QListWidgetItem*)));

    // --- 2. tus kaydi
    kayitS = sayfa(lay);
    lay->addWidget(new QLabel(Dil::t("<b>2/3</b> Tuşu tanıtın")));
    kayitYazi = new QLabel;
    kayitYazi->setWordWrap(true);
    kayitTus = new QLabel;
    lay->addWidget(kayitYazi);
    lay->addWidget(kayitTus);
    lay->addStretch();
    QPushButton *g3 = new QPushButton(Dil::t("Vazgeç"));
    lay->addWidget(g3);
    connect(g3, SIGNAL(clicked()), this, SLOT(kayitIptal()));

    // --- 3. tetik
    tetikS = sayfa(lay);
    lay->addWidget(new QLabel(Dil::t("<b>3/3</b> Nasıl çalışsın?")));
    tetikUyari = new QLabel;
    tetikUyari->setWordWrap(true);
    lay->addWidget(tetikUyari);
    tetikListe = new QListWidget;
    struct { const char *ad; int tetik; int sayi; } tetikler[] = {
        { QT_TR_NOOP("Bas-çek (tek basış)"), ETetikBas, 1 },
        { QT_TR_NOOP("Peş peşe 2 kez bas"), ETetikCok, 2 },
        { QT_TR_NOOP("Peş peşe 3 kez bas"), ETetikCok, 3 },
        { QT_TR_NOOP("Basılı tut"), ETetikUzun, 1 } };
    for (unsigned i = 0; i < sizeof(tetikler) / sizeof(tetikler[0]); ++i) {
        QListWidgetItem *it = new QListWidgetItem(Dil::t(tetikler[i].ad), tetikListe);
        it->setData(Qt::UserRole, tetikler[i].tetik);
        it->setData(Qt::UserRole + 1, tetikler[i].sayi);
    }
    lay->addWidget(tetikListe, 1);
    QPushButton *g4 = new QPushButton(Dil::t("Vazgeç"));
    lay->addWidget(g4);
    connect(g4, SIGNAL(clicked()), this, SLOT(anaSayfa()));
    connect(tetikListe, SIGNAL(itemActivated(QListWidgetItem*)), this, SLOT(tetikSecildi(QListWidgetItem*)));

    // --- test
    testS = sayfa(lay);
    lay->addWidget(new QLabel(Dil::t("Atadığınız tuşları deneyin. Servis bu sırada duraklatılır.")));
    QHBoxLayout *s3 = new QHBoxLayout;
    testCalistir = new QCheckBox(Dil::t("Eylemi de çalıştır"));
    testHam = new QCheckBox(Dil::t("Ham tuş kodları"));
    s3->addWidget(testCalistir); s3->addWidget(testHam);
    lay->addLayout(s3);
    testLog = new QListWidget;
    lay->addWidget(testLog, 1);
    QPushButton *g5 = new QPushButton(Dil::t("Bitir"));
    lay->addWidget(g5);
    connect(g5, SIGNAL(clicked()), this, SLOT(testBitir()));

    // sag secim tusu: geri / cikis
    QAction *geriA = new QAction(Dil::t("Geri"), &pencere);
    geriA->setSoftKeyRole(QAction::NegativeSoftKey);
    connect(geriA, SIGNAL(triggered()), this, SLOT(geri()));
    pencere.addAction(geriA);

    TRAPD(err, motor = CMotor::NewL(CCoeEnv::Static()->WsSession(), CCoeEnv::Static()->RootWin(), *this));
    if (err == KErrNone) TRAP_IGNORE(motor->YukleL());
    servisBaslat();
    anaSayfa();
    pencere.showMaximized();
    QTimer::singleShot(800, this, SLOT(durumYenile()));
}

// ---------------------------------------------------------------- ana sayfa
void Uygulama::anaSayfa()
{
    if (kip == KKayit1 || kip == KKayit2) kayitIptal();
    if (kip == KTest) testBitir();
    if (motor) TRAP_IGNORE(motor->YukleL());
    listeYenile();
    durumYenile();
    yigin->setCurrentWidget(anaS);
    liste->setFocus();
}

void Uygulama::listeYenile()
{
    liste->clear();
    if (!motor) return;
    RArray<TKisayol> &k = motor->Kisayollar();
    for (TInt i = 0; i < k.Count(); ++i)
        new QListWidgetItem(QString::fromUtf8("%1 • %2 → %3").arg(tusAdi(k[i].iScan, k[i].iCode))
                                .arg(tetikAdi(k[i].iTetik, k[i].iSayi)).arg(eylemAdi(k[i].iEylem, k[i].iParam)), liste);
    if (!k.Count()) new QListWidgetItem(Dil::t("Kısayol yok. \"Yeni kısayol\" ile ekleyin."), liste);
    titresimK->blockSignals(true);
    titresimK->setChecked(motor->iTitresim);
    titresimK->blockSignals(false);
}

void Uygulama::durumYenile()
{
    const bool calisiyor = servisWg() != 0;
    QString ek;
    QFile f(QString::fromLatin1("C:/Data/BelleMiddleKey/durum.txt"));
    if (calisiyor && f.open(QIODevice::ReadOnly)) {
        const QList<QByteArray> p = f.readAll().trimmed().split(' ');
        if (p.size() >= 3 && p[0] == "calisiyor" && p[2].toInt() > 0)
            ek = Dil::t(" Uyarı: %1 tuş yakalanamadı.").arg(p[2].toInt());
    }
    durum->setText((calisiyor ? Dil::t("Servis çalışıyor: atanan tuşlar her uygulamada dinleniyor.")
                              : Dil::t("Servis durdu: kısayollar çalışmaz.")) + ek);
    servisBtn->setText(calisiyor ? Dil::t("Servisi durdur") : Dil::t("Servisi başlat"));
}

void Uygulama::servisDegistir()
{
    if (servisWg()) servisGonder(KBmkCikis);
    else servisBaslat();
    QTimer::singleShot(700, this, SLOT(durumYenile()));
}

void Uygulama::titresimDegisti(bool v)
{
    if (!motor) return;
    motor->iTitresim = v;
    TRAP_IGNORE(motor->KaydetL());
    servisGonder(KBmkYenile);
}

void Uygulama::sil()
{
    if (!motor) return;
    const int r = liste->currentRow();
    RArray<TKisayol> &k = motor->Kisayollar();
    if (r < 0 || r >= k.Count()) return;
    k.Remove(r);
    TRAP_IGNORE(motor->KaydetL());
    servisGonder(KBmkYenile);
    listeYenile();
    liste->setCurrentRow(qMin(r, liste->count() - 1));
}

void Uygulama::geri()
{
    QWidget *c = yigin->currentWidget();
    if (c == anaS) { quit(); return; }
    if (c == uygS) { yigin->setCurrentWidget(eylemS); eylemListe->setFocus(); return; }
    anaSayfa();
}

// ---------------------------------------------------------------- kisayol ekleme
void Uygulama::yeniKisayol()
{
    yeni.iEylem = EEylemSessiz;
    yeni.iParam = 0;
    yigin->setCurrentWidget(eylemS);
    eylemListe->setCurrentRow(0);
    eylemListe->setFocus();
}

void Uygulama::eylemSecildi(QListWidgetItem *it)
{
    yeni.iEylem = it->data(Qt::UserRole).toInt();
    yeni.iParam = it->data(Qt::UserRole + 1).toUInt();
    if (yeni.iEylem == EEylemUygulama && yeni.iParam == 0) {
        if (!uygListe->count()) {
            // gizli olmayan tum uygulamalar, ada gore sirali
            QList<QPair<QString, uint> > l;
            RApaLsSession ls;
            if (ls.Connect() == KErrNone) {
                if (ls.GetAllApps() == KErrNone) {
                    TApaAppInfo info;
                    while (ls.GetNextApp(info) == KErrNone) {
                        TApaAppCapabilityBuf cap;
                        if (ls.GetAppCapability(cap, info.iUid) == KErrNone && cap().iAppIsHidden) continue;
                        const QString ad = QString::fromUtf16(info.iCaption.Ptr(), info.iCaption.Length());
                        if (!ad.isEmpty()) l.append(qMakePair(ad, (uint)info.iUid.iUid));
                    }
                }
                ls.Close();
            }
            qSort(l);
            for (int i = 0; i < l.size(); ++i) {
                QListWidgetItem *u = new QListWidgetItem(l[i].first, uygListe);
                u->setData(Qt::UserRole, l[i].second);
            }
        }
        yigin->setCurrentWidget(uygS);
        uygListe->setCurrentRow(0);
        uygListe->setFocus();
        return;
    }
    kayitBasla();
}

void Uygulama::uygulamaSecildi(QListWidgetItem *it)
{
    yeni.iParam = it->data(Qt::UserRole).toUInt();
    kayitBasla();
}

void Uygulama::kayitBasla()
{
    kip = KKayit1;
    kayitScan = -1;
    kayitKod = 0;
    kayitYazi->setText(Dil::t("Atamak istediğiniz tuşa <b>bir kez basıp bırakın</b>."));
    kayitTus->setText(QString());
    yigin->setCurrentWidget(kayitS);
    servisGonder(KBmkDuraklat);
    kayitYakala();
}

// TusTest yontemi: cihaz/uygulama tuslarini (orta tus, ses tuslari, menu...) yuksek oncelikle yakala.
// Sol/sag secim tuslari ve yon tusu ortasi (Device0..3) haric: arayuz kullanilabilir kalsin.
void Uygulama::kayitYakala()
{
    kayitBirak();
    RWindowGroup &wg = CCoeEnv::Static()->RootWin();
    for (TInt sc = EStdKeyIncVolume; sc <= 0xFF; ++sc) {
        if (sc == EStdKeyDevice0 || sc == EStdKeyDevice1 || sc == EStdKeyDevice2 || sc == EStdKeyDevice3) continue;
        const TInt32 h = wg.CaptureKeyUpAndDowns(sc, 0, 0, KMaxTInt);
        if (h > 0) kayitYakalama.append(qMakePair(h, 0));
    }
    for (TUint c = EKeyDevice4; c <= EKeyDevice4 + 0x60; ++c) {
        TInt32 h = wg.CaptureKey(c, 0, 0, KMaxTInt);
        if (h > 0) kayitYakalama.append(qMakePair(h, 1));
        h = wg.CaptureLongKey(c, c, 0, 0, KMaxTInt, ELongCaptureNormal);
        if (h > 0) kayitYakalama.append(qMakePair(h, 2));
    }
}

void Uygulama::kayitBirak()
{
    if (kayitYakalama.isEmpty()) return;
    RWindowGroup &wg = CCoeEnv::Static()->RootWin();
    for (int i = 0; i < kayitYakalama.size(); ++i) {
        const TInt32 h = kayitYakalama[i].first;
        if (kayitYakalama[i].second == 0) wg.CancelCaptureKeyUpAndDowns(h);
        else if (kayitYakalama[i].second == 1) wg.CancelCaptureKey(h);
        else wg.CancelCaptureLongKey(h);
    }
    kayitYakalama.clear();
}

void Uygulama::kayitIptal()
{
    kayitBirak();
    if (kip == KKayit1 || kip == KKayit2) servisGonder(KBmkSurdur);
    kip = KNormal;
    anaSayfa();
}

bool Uygulama::kayitOlayi(int tur, const TKeyEvent &k)
{
    const int sc = k.iScanCode;
    if (sc == EStdKeyDevice1 || sc == EStdKeyDevice0) return false;   // secim tuslari: vazgec / menu
    if (tur == EEventKey) {
        if (sc == kayitScan && k.iCode && !kayitKod) kayitKod = k.iCode;
        return true;
    }
    if (sc == EStdKeyLeftShift || sc == EStdKeyRightShift || sc == EStdKeyLeftCtrl || sc == EStdKeyRightCtrl
        || sc == EStdKeyLeftFunc || sc == EStdKeyRightFunc || sc == EStdKeyLeftAlt || sc == EStdKeyRightAlt)
        return true;
    if (tur == EEventKeyDown) {
        if (kip == KKayit1) { kayitScan = sc; kayitKod = 0; }
        else if (sc != kayitScan) {
            kip = KKayit1; kayitScan = sc; kayitKod = 0;
            kayitYazi->setText(Dil::t("Farklı bir tuş algılandı. Aynı tuşa <b>bir kez basıp bırakın</b>."));
        }
        basisSure.start();
        kayitTus->setText(tusAdi(sc, kayitKod));
        return true;
    }
    // EEventKeyUp
    if (sc != kayitScan) return true;
    const qint64 ms = basisSure.elapsed();
    kayitTus->setText(tusAdi(sc, kayitKod));
    if (kip == KKayit1) {
        if (ms >= 600) { kayitYazi->setText(Dil::t("Önce kısa basın: tuşa <b>bir kez basıp bırakın</b>.")); return true; }
        kip = KKayit2;
        kayitYazi->setText(Dil::t("Şimdi aynı tuşu <b>1 saniye basılı tutun</b>."));
        return true;
    }
    if (ms < 600) { kayitYazi->setText(Dil::t("Biraz daha uzun: tuşu <b>1 saniye basılı tutun</b>.")); return true; }
    // kayit tamam
    kayitBirak();
    kip = KNormal;
    yeni.iScan = kayitScan;
    yeni.iCode = kayitKod;
    const QString ad = tusAdi(kayitScan, kayitKod);
    QString uyari = Dil::t("Tuş: <b>%1</b> → %2").arg(ad).arg(eylemAdi(yeni.iEylem, yeni.iParam));
    if (yaziTusu(kayitScan) || kayitScan == EStdKeyNo || kayitScan == EStdKeyYes)
        uyari += Dil::t("<br>Bu tuş başka işlerde de kullanılıyor: yalnızca \"Basılı tut\" önerilir (kısa basış normal çalışmaya devam eder).");
    if (!kayitKod)
        uyari += Dil::t("<br>Bu tuşun tuş kodu yok: \"Basılı tut\" seçilse de kısa basışlar yakalanır.");
    tetikUyari->setText(uyari);
    yigin->setCurrentWidget(tetikS);
    tetikListe->setCurrentRow(yaziTusu(kayitScan) ? 3 : 0);
    tetikListe->setFocus();
    servisGonder(KBmkSurdur);
    return true;
}

void Uygulama::tetikSecildi(QListWidgetItem *it)
{
    if (!motor) return;
    yeni.iTetik = it->data(Qt::UserRole).toInt();
    yeni.iSayi = it->data(Qt::UserRole + 1).toInt();
    RArray<TKisayol> &k = motor->Kisayollar();
    // ayni tus + ayni tetik varsa yerine gecer
    for (TInt i = k.Count() - 1; i >= 0; --i)
        if (k[i].iScan == yeni.iScan && k[i].iTetik == yeni.iTetik && (yeni.iTetik != ETetikCok || k[i].iSayi == yeni.iSayi))
            k.Remove(i);
    k.Append(yeni);
    TRAP_IGNORE(motor->KaydetL());
    if (servisWg()) servisGonder(KBmkYenile); else servisBaslat();
    anaSayfa();
    liste->setCurrentRow(liste->count() - 1);
}

// ---------------------------------------------------------------- test
void Uygulama::testBasla()
{
    if (!motor) return;
    testLog->clear();
    logla(Dil::t("Hazır. Atadığınız tuşlara basın."));
    yigin->setCurrentWidget(testS);
    servisGonder(KBmkDuraklat);
    TRAP_IGNORE(motor->YukleL());
    motor->Yakala();
    if (motor->YakalamaHatasi()) logla(Dil::t("Uyarı: %1 tuş yakalanamadı.").arg(motor->YakalamaHatasi()));
    kip = KTest;
}

void Uygulama::testBitir()
{
    if (kip != KTest) return;
    kip = KNormal;
    if (motor) motor->Birak();
    servisGonder(KBmkSurdur);
    anaSayfa();
}

void Uygulama::logla(const QString &s)
{
    testLog->insertItem(0, s);
    while (testLog->count() > 60) delete testLog->takeItem(testLog->count() - 1);
}

void Uygulama::Tetiklendi(const TKisayol &k)
{
    QString s = QString::fromUtf8("✓ %1 • %2 → %3").arg(tusAdi(k.iScan, k.iCode)).arg(tetikAdi(k.iTetik, k.iSayi))
                    .arg(eylemAdi(k.iEylem, k.iParam));
    if (testCalistir->isChecked()) {
        TInt kapatilan = 0;
        const TInt err = Eylem::Calistir(CCoeEnv::Static()->WsSession(), k, motor->iTitresim, &kapatilan);
        if (err != KErrNone) s += Dil::t(" (hata %1)").arg(err);
        else if (k.iEylem == EEylemKapat) s += Dil::t(" (%1 uygulama)").arg(kapatilan);
    } else if (motor->iTitresim) {
        Eylem::Titret(40);
    }
    logla(s);
}

void Uygulama::Eslesmedi(TInt scan, TUint code, TInt tetik, TInt sayi)
{
    logla(Dil::t("– %1 • %2: atanmış kısayol yok").arg(tusAdi(scan, code)).arg(tetikAdi(tetik, sayi)));
}

// ---------------------------------------------------------------- olaylar
bool Uygulama::symbianEventFilter(const QSymbianEvent *ev)
{
    if (kip != KNormal && ev->type() == QSymbianEvent::WindowServerEvent) {
        const TWsEvent *w = ev->windowServerEvent();
        const TInt t = w->Type();
        if (t == EEventKey || t == EEventKeyDown || t == EEventKeyUp) {
            const TKeyEvent *k = w->Key();
            if (kip == KKayit1 || kip == KKayit2) {
                if (kayitOlayi(t, *k)) return true;
            } else if (kip == KTest) {
                if (testHam->isChecked())
                    logla(QString::fromLatin1("%1 scan=0x%2 code=0x%3 rep=%4")
                              .arg(QString::fromLatin1(t == EEventKey ? "KEY " : t == EEventKeyDown ? "DOWN" : "UP  "))
                              .arg((uint)k->iScanCode, 0, 16).arg((uint)k->iCode, 0, 16).arg(k->iRepeats));
                TBool tuket = EFalse;
                TRAP_IGNORE(tuket = motor->OlayL(*w));
                if (tuket) return true;
            }
        }
    }
    return QApplication::symbianEventFilter(ev);
}

int main(int argc, char *argv[])
{
    Uygulama app(argc, argv);
    Dil::kur(&app);   // Turkce / English
    app.kur();
    return app.exec();
}

#include "main.moc"
