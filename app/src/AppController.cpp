#include "AppController.h"

#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QMetaObject>
#include <QPointer>
#include <QThreadPool>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "agrovis/capture_scanner.hpp"
#include "agrovis/engine.hpp"

namespace agrovis::app {
namespace {

QImage toQImage(const agrovis::RgbaImage& src) {
    if (src.empty()) return {};
    QImage view(src.pixels.data(), src.width, src.height, src.width * 4, QImage::Format_RGBA8888);
    return view.copy();
}

void computeAutoFitBounds(const agrovis::WorkbenchOutput& out, float& minOut, float& maxOut) {
    const size_t n = out.indexValues.size();
    if (n == 0) return;

    std::vector<float> samples;
    samples.reserve(4096);
    const size_t stride = std::max<size_t>(1, n / 40000);
    const bool hasMask = (out.validMask.size() == n);

    for (size_t i = 0; i < n; i += stride) {
        if (hasMask && out.validMask[i] == 0) continue;
        const float v = out.indexValues[i];
        if (std::isfinite(v)) samples.push_back(v);
    }
    if (samples.size() < 16) {
        samples.clear();
        for (size_t i = 0; i < n; i += stride) {
            const float v = out.indexValues[i];
            if (std::isfinite(v)) samples.push_back(v);
        }
    }
    if (samples.empty()) return;

    std::sort(samples.begin(), samples.end());
    float lo = samples[(samples.size() - 1) * 2 / 100];
    float hi = samples[(samples.size() - 1) * 98 / 100];
    if (hi - lo < 0.08f) {
        const float mid = 0.5f * (lo + hi);
        lo = mid - 0.04f;
        hi = mid + 0.04f;
    }
    minOut = std::floor(lo * 100.0f) / 100.0f;
    maxOut = std::ceil(hi * 100.0f) / 100.0f;
}

struct LocalizedIndexText {
    agrovis::SpectralIndex id;
    const char* jaShort;
    const char* enShort;
    const char* jaFull;
    const char* enFull;
    const char* jaUseCase;
    const char* enUseCase;
    const char* jaColorMeaning;
    const char* enColorMeaning;
};

const LocalizedIndexText& localizedInfoFor(agrovis::SpectralIndex id) {
    static const std::array<LocalizedIndexText, 12> kTable{{
        {
            agrovis::SpectralIndex::RGB,
            "通常写真", "RGB",
            "通常カラー画像 (RGB)", "True Color RGB",
            "倒伏・雑草・獣害・水たまり・農機の跡など、肉眼で見て分かる異常を確認するときに使います。",
            "Visual check for physical damage, weeds, lodging, standing water, or equipment tracks.",
            "カメラで見たそのままの色で表示します（特別な色分けは行いません）。",
            "Shows natural camera colors as seen by the human eye (no health color map applied)."
        },
        {
            agrovis::SpectralIndex::NDVI,
            "生育活力", "NDVI",
            "正規化植生指数 (NDVI)", "Normalized Difference Vegetation Index (NDVI)",
            "まずはこれから確認する基本の指標です。作物全体の元気さ・葉の茂り具合や、生育が弱い場所を素早く見つけます。",
            "Primary all-around health check. Shows overall crop vigor, leaf density, and weak spots early.",
            "緑＝よく茂り元気 / 黄・橙＝やや薄い・注意 / 赤＝生育不良・傷み / 濃い灰色＝土や道などの除外部分。",
            "Green = dense, vigorous crop / Yellow-Amber = thin or mild stress / Red = severe stress or poor cover / Dark gray = bare soil or masked."
        },
        {
            agrovis::SpectralIndex::NDRE,
            "窒素・成熟", "NDRE",
            "正規化レッドエッジ指数 (NDRE)", "Normalized Difference Red Edge (NDRE)",
            "作物が大きく茂って「生育活力(NDVI)」が全体緑に見える時期や、追肥の判断・登熟期の確認に適しています（赤端センサー必要）。",
            "Best once canopy closes and NDVI saturates. Tracks nitrogen status, chlorophyll, and late-season stress (needs Red-Edge band).",
            "緑＝葉色が濃く窒素十分 / 黄＝肥料不足や成熟の始まり / 赤＝強いストレス・枯れ始め / 濃い灰色＝土や除外部分。",
            "Green = rich chlorophyll & nitrogen / Yellow = early deficiency or ripening / Red = heavy stress / Dark gray = soil or masked."
        },
        {
            agrovis::SpectralIndex::GNDVI,
            "緑葉活力", "GNDVI",
            "緑色正規化植生指数 (GNDVI)", "Green Normalized Difference Vegetation Index (GNDVI)",
            "葉緑素（クロロフィル）の変化に敏感で、広葉作物や成長中期の水分・窒素ストレスの把握に向いています。",
            "Sensitive to leaf chlorophyll concentration; useful for mid-season nitrogen and water uptake checks.",
            "緑＝葉緑素が多く健全 / 黄＝やや葉色が薄い / 赤＝窒素不足や乾燥ストレスの疑い / 濃い灰色＝土など除外。",
            "Green = strong chlorophyll content / Yellow = fading leaf greenness / Red = likely nitrogen or water stress / Dark gray = masked."
        },
        {
            agrovis::SpectralIndex::SAVI,
            "土壌補正", "SAVI",
            "土壌調整植生指数 (SAVI)", "Soil-Adjusted Vegetation Index (SAVI)",
            "苗の植え付け直後など、株の間から地面（土）がたくさん見えている畑で、土の影響を抑えて作物の元気さを測ります。",
            "Use when plants are small and bare soil is visible between rows so soil brightness doesn't skew vigor scores.",
            "緑＝株が順調に生育 / 黄＝やや小ぶり・むらあり / 赤＝発育遅れ / 濃い灰色＝露出した土などの除外領域。",
            "Green = healthy plant stands / Yellow = uneven or light cover / Red = weak growth / Dark gray = exposed soil or masked."
        },
        {
            agrovis::SpectralIndex::OSAVI,
            "土壌最適化", "OSAVI",
            "最適化土壌調整指数 (OSAVI)", "Optimized Soil-Adjusted Vegetation Index (OSAVI)",
            "「土壌補正(SAVI)」を改良し、乾いた土や湿った土が混ざる圃場でも、株の生育差をより安定して見分けます。",
            "Refined version of SAVI that handles varying dry and wet soil brightness more consistently.",
            "緑＝安定した生育 / 黄＝注意エリア / 赤＝生育低下 / 濃い灰色＝土や畦などの除外部分。",
            "Green = consistent vigorous canopy / Yellow = moderate variation / Red = weak spots / Dark gray = soil or masked."
        },
        {
            agrovis::SpectralIndex::MSAVI2,
            "発芽初期", "MSAVI2",
            "改良土壌調整指数2 (MSAVI2)", "Modified Soil-Adjusted Vegetation Index 2 (MSAVI2)",
            "発芽直後〜生育ごく初期で、画面のほとんどが土のときに使います。土の反射を自動で抑えて小さな芽を見分けます。",
            "Best at germination and very early emergence when most of the frame is still bare soil.",
            "緑＝芽や葉がしっかり育っている / 黄＝生育途上・むら / 赤＝欠株や発芽不良 / 濃い灰色＝土面。",
            "Green = strong early seedlings / Yellow = sparse or developing / Red = missing stand or poor emergence / Dark gray = soil."
        },
        {
            agrovis::SpectralIndex::EVI,
            "密集補正", "EVI",
            "拡張植生指数 (EVI)", "Enhanced Vegetation Index (EVI)",
            "草丈が高く葉が何重にも重なった圃場や、日差しが強く少しもやがある日でも色つぶれしにくい指標です。",
            "Designed for very thick, tall canopies and bright or slightly hazy sunlight without saturating easily.",
            "緑＝非常に旺盛な繁茂 / 黄＝中程度の葉量 / 赤＝葉量が少ない・衰え / 濃い灰色＝土や除外部分。",
            "Green = very dense active foliage / Yellow = moderate biomass / Red = thin canopy or decline / Dark gray = masked."
        },
        {
            agrovis::SpectralIndex::CIgreen,
            "葉緑素(緑)", "CIgreen",
            "緑葉クロロフィル指数 (CIgreen)", "Chlorophyll Index (Green)",
            "葉の中にどれだけ葉緑素が詰まっているかを直接近い形で表します。数値が0〜8と大きく変化し、追肥の判断材料になります。",
            "Estimates leaf chlorophyll directly over a wider numeric scale (0 to 8), helping judge fertilizer needs.",
            "緑（数値高）＝葉緑素が豊富 / 黄＝標準的 / 赤（数値低）＝葉緑素が少なく追肥検討 / 濃い灰色＝土など除外。",
            "Green (high value) = high chlorophyll / Yellow = moderate / Red (low value) = low chlorophyll, check nitrogen / Dark gray = masked."
        },
        {
            agrovis::SpectralIndex::CIrededge,
            "葉緑素(赤端)", "CIre",
            "赤端クロロフィル指数 (CIre)", "Chlorophyll Index (Red Edge)",
            "「葉緑素(緑)」の赤端バンド版です。葉が重なり合った時期でも窒素不足の差をくっきり出します（赤端センサー必要）。",
            "Red-Edge version of CIgreen; separates nitrogen differences cleanly even inside dense canopies (needs Red-Edge band).",
            "緑（数値高）＝窒素・葉緑素が十分 / 黄＝やや低下 / 赤（数値低）＝窒素不足の兆候 / 濃い灰色＝除外。",
            "Green (high) = ample chlorophyll / Yellow = mild drop / Red (low) = early nitrogen stress / Dark gray = masked."
        },
        {
            agrovis::SpectralIndex::LCI,
            "葉緑素比", "LCI",
            "葉クロロフィル指数 (LCI)", "Leaf Chlorophyll Index (LCI)",
            "成熟期に向かう作物の葉緑素の抜け方や、圃場内の微細な栄養ムラの確認に使います（赤端センサー必要）。",
            "Highlights subtle chlorophyll variation and senescence gradients across maturing crops (needs Red-Edge band).",
            "緑＝青々として葉緑素を維持 / 黄＝成熟・変化の途中 / 赤＝黄化やストレス進行 / 濃い灰色＝除外。",
            "Green = active green chlorophyll / Yellow = transitioning or maturing / Red = yellowing or stress / Dark gray = masked."
        },
        {
            agrovis::SpectralIndex::VARI,
            "簡易生育", "VARI",
            "可視光植生指数 (VARI)", "Visible Atmospherically Resistant Index (VARI)",
            "近赤外カメラがない「普通のカラー写真（RGB）」だけから緑の濃さを推定する簡易法です。専用カメラがある場合は他の指標を推奨します。",
            "Approximate greenness estimate using standard RGB colors only (works without NIR, but less accurate than NDVI/NDRE).",
            "緑＝見た目の緑が濃い / 黄＝黄緑・薄い / 赤＝茶色・枯れ色や土に近い色 / 濃い灰色＝除外部分。",
            "Green = visually greener foliage / Yellow = pale or yellowish / Red = brown, dry, or soil-toned / Dark gray = masked."
        },
    }};

    for (const auto& entry : kTable) {
        if (entry.id == id) return entry;
    }
    return kTable[1];
}

}  // namespace

AppController::AppController(
    CaptureListModel* captureModel,
    ViewportImageProvider* imageProvider,
    QObject* parent)
    : QObject(parent), captureModel_(captureModel), imageProvider_(imageProvider) {
    loadingDelayTimer_.setSingleShot(true);
    connect(&loadingDelayTimer_, &QTimer::timeout, this, [this]() {
        if (!isLoading_) {
            isLoading_ = true;
            emit loadingChanged();
        }
    });

    // Populate initial synthetic plot synchronously so startup renders immediately.
    currentOutput_ = agrovis::SpectralEngine::processSyntheticSample(config_);
    imageProvider_->setImages(
        toQImage(currentOutput_.baseRgb),
        toQImage(currentOutput_.indexOverlay));
    ++imageRevision_;
}

AppController::~AppController() {
    activeJobSeq_->fetch_add(1);
    QThreadPool::globalInstance()->waitForDone();
}

void AppController::setActiveTab(int tab) {
    if (activeTab_ == tab) return;
    activeTab_ = tab;
    emit uiStateChanged();
}

void AppController::setLanguage(const QString& lang) {
    const QString normalized = (lang == QLatin1String("en")) ? QStringLiteral("en") : QStringLiteral("ja");
    if (language_ == normalized) return;
    language_ = normalized;
    emit languageChanged();
    emit configChanged();
    emit outputUpdated();
}

QVariantMap AppController::tr() const {
    const bool ja = (language_ != QLatin1String("en"));
    QVariantMap m;

    // Window & Top Bar
    m[QStringLiteral("windowTitle")] = ja
        ? QStringLiteral("AGROVIS - 農作物マルチスペクトル診断")
        : QStringLiteral("AGROVIS - Crop Multispectral Scouting");
    m[QStringLiteral("appSubtitle")] = ja
        ? QStringLiteral("圃場作物チェック")
        : QStringLiteral("FIELD CROP SCOUTING");
    m[QStringLiteral("tabAnalysis")] = ja ? QStringLiteral("解析ビュー") : QStringLiteral("Analysis");
    m[QStringLiteral("tabGuide")] = ja ? QStringLiteral("指標ガイド") : QStringLiteral("Spectral Index Information");
    m[QStringLiteral("openFolder")] = ja ? QStringLiteral("フォルダを開く") : QStringLiteral("Open Folder");
    m[QStringLiteral("exportPng")] = ja ? QStringLiteral("画像保存") : QStringLiteral("Export PNG");
    m[QStringLiteral("folderDialogTitle")] = ja
        ? QStringLiteral("撮影画像フォルダを選択")
        : QStringLiteral("Select Multispectral Capture Folder");
    m[QStringLiteral("exportDialogTitle")] = ja
        ? QStringLiteral("解析画像をPNGで保存")
        : QStringLiteral("Export Processed Spectral Overlay");
    m[QStringLiteral("processedPrefix")] = ja ? QStringLiteral("処理時間: ") : QStringLiteral("Processed in ");

    // Left Capture Sidebar
    m[QStringLiteral("captureQueue")] = ja ? QStringLiteral("撮影データ一覧") : QStringLiteral("CAPTURE QUEUE");
    m[QStringLiteral("setsSuffix")] = ja ? QStringLiteral(" 件") : QStringLiteral(" sets");
    m[QStringLiteral("loadDir")] = ja ? QStringLiteral("+ フォルダ読込") : QStringLiteral("+ Load Capture Dir");
    m[QStringLiteral("sampleBtn")] = ja ? QStringLiteral("サンプル圃場") : QStringLiteral("Sample Plot");
    m[QStringLiteral("sampleTitle")] = ja ? QStringLiteral("テスト用サンプル圃場") : QStringLiteral("Built-in Sample Plot");
    m[QStringLiteral("sampleBadge")] = ja ? QStringLiteral("5バンド") : QStringLiteral("5-BAND");
    m[QStringLiteral("sampleDesc")] = ja
        ? QStringLiteral("作物列・ストレス区画・基準板")
        : QStringLiteral("Crop rows + stress pocket + 50% gray ref card");
    m[QStringLiteral("filesSuffix")] = ja ? QStringLiteral("枚") : QStringLiteral(" files");
    m[QStringLiteral("emptyQueueHint")] = ja
        ? QStringLiteral("カメラやドローンで撮影した画像のフォルダを開くと、自動で一覧に読み込まれます。")
        : QStringLiteral("Open a folder with your multispectral or camera images to load captures automatically.");
    m[QStringLiteral("bandRgb")] = ja ? QStringLiteral("カラー") : QStringLiteral("RGB");
    m[QStringLiteral("bandG")] = ja ? QStringLiteral("緑") : QStringLiteral("G");
    m[QStringLiteral("bandR")] = ja ? QStringLiteral("赤") : QStringLiteral("R");
    m[QStringLiteral("bandRe")] = ja ? QStringLiteral("赤端") : QStringLiteral("RE");
    m[QStringLiteral("bandNir")] = ja ? QStringLiteral("近赤外") : QStringLiteral("NIR");

    // Center Viewport
    m[QStringLiteral("modeSplit")] = ja ? QStringLiteral("左右比較") : QStringLiteral("Split Curtain");
    m[QStringLiteral("modeSide")] = ja ? QStringLiteral("並べて表示") : QStringLiteral("Side-by-Side");
    m[QStringLiteral("modeOverlay")] = ja ? QStringLiteral("重ね合わせ") : QStringLiteral("Overlay");
    m[QStringLiteral("modeIndexOnly")] = ja ? QStringLiteral("マップのみ") : QStringLiteral("Index Only");
    m[QStringLiteral("opacityLabel")] = ja ? QStringLiteral("濃さ ") : QStringLiteral("Opacity ");
    m[QStringLiteral("paletteLabel")] = ja ? QStringLiteral("配色") : QStringLiteral("Palette");
    m[QStringLiteral("palRdYlGn")] = ja ? QStringLiteral("赤黄緑 (標準)") : QStringLiteral("RdYlGn");
    m[QStringLiteral("palViridis")] = ja ? QStringLiteral("青緑黄") : QStringLiteral("Viridis");
    m[QStringLiteral("palTurbo")] = ja ? QStringLiteral("虹色") : QStringLiteral("Turbo");
    m[QStringLiteral("palGray")] = ja ? QStringLiteral("白黒") : QStringLiteral("Grayscale");
    m[QStringLiteral("zoomFit")] = ja ? QStringLiteral("全体") : QStringLiteral("Fit");
    m[QStringLiteral("baseBadge")] = ja ? QStringLiteral("通常画像") : QStringLiteral("RGB BASE");
    m[QStringLiteral("trueColorPane")] = ja ? QStringLiteral("元画像（通常カラー）") : QStringLiteral("TRUE COLOR / BASE");
    m[QStringLiteral("spectralMapSuffix")] = ja ? QStringLiteral(" マップ") : QStringLiteral(" SPECTRAL MAP");
    m[QStringLiteral("loading")] = ja ? QStringLiteral("読み込み中...") : QStringLiteral("Loading...");
    m[QStringLiteral("hoverPrompt")] = ja
        ? QStringLiteral("画像にカーソルを合わせると、各地点の数値を確認できます")
        : QStringLiteral("Hover canvas to inspect per-band reflectance & index value");
    m[QStringLiteral("maskedTag")] = ja ? QStringLiteral(" [除外]") : QStringLiteral(" [MASKED]");
    m[QStringLiteral("scaleLow")] = ja ? QStringLiteral("弱い(赤)") : QStringLiteral("Weak");
    m[QStringLiteral("scaleHigh")] = ja ? QStringLiteral("元気(緑)") : QStringLiteral("Good");
    m[QStringLiteral("standardBtn")] = ja ? QStringLiteral("標準表示") : QStringLiteral("Standard");
    m[QStringLiteral("autoFitBtn")] = ja ? QStringLiteral("差を強調") : QStringLiteral("Boost Contrast");
    m[QStringLiteral("probeB")] = ja ? QStringLiteral("青:") : QStringLiteral("B:");
    m[QStringLiteral("probeG")] = ja ? QStringLiteral("緑:") : QStringLiteral("G:");
    m[QStringLiteral("probeR")] = ja ? QStringLiteral("赤:") : QStringLiteral("R:");
    m[QStringLiteral("probeRe")] = ja ? QStringLiteral("赤端:") : QStringLiteral("RE:");
    m[QStringLiteral("probeNir")] = ja ? QStringLiteral("近赤外:") : QStringLiteral("NIR:");

    // Right Inspector
    m[QStringLiteral("spectralIndexHeader")] = ja ? QStringLiteral("解析方法") : QStringLiteral("SPECTRAL INDEX");
    m[QStringLiteral("bgFilterHeader")] = ja ? QStringLiteral("背景の除外設定") : QStringLiteral("BACKGROUND FILTER");
    m[QStringLiteral("maskSoil")] = ja ? QStringLiteral("土を除外") : QStringLiteral("Mask Soil");
    m[QStringLiteral("maskWater")] = ja ? QStringLiteral("水を除外") : QStringLiteral("Water");
    m[QStringLiteral("maskShadow")] = ja ? QStringLiteral("影を除外") : QStringLiteral("Shadow");
    m[QStringLiteral("maskGlare")] = ja ? QStringLiteral("反射光") : QStringLiteral("Glare");
    m[QStringLiteral("healthHeader")] = ja ? QStringLiteral("作物の健康状態（目安）") : QStringLiteral("CROP HEALTH ESTIMATE");
    m[QStringLiteral("healthy")] = ja ? QStringLiteral("良好") : QStringLiteral("Healthy");
    m[QStringLiteral("moderate")] = ja ? QStringLiteral("やや注意") : QStringLiteral("Moderate");
    m[QStringLiteral("stressed")] = ja ? QStringLiteral("要確認") : QStringLiteral("Stressed");
    m[QStringLiteral("soilMask")] = ja ? QStringLiteral("背景・除外") : QStringLiteral("Excluded");
    m[QStringLiteral("canopyKpi")] = ja ? QStringLiteral("被覆率") : QStringLiteral("CANOPY");
    m[QStringLiteral("meanKpi")] = ja ? QStringLiteral("平均値") : QStringLiteral("MEAN");
    m[QStringLiteral("stdDevKpi")] = ja ? QStringLiteral("ばらつき") : QStringLiteral("STD DEV");
    m[QStringLiteral("roughIssueHeader")] = ja ? QStringLiteral("推定される症状") : QStringLiteral("ROUGH ISSUE ESTIMATE");
    m[QStringLiteral("roughIssueValue")] = ja ? QStringLiteral("開発中 (WIP)") : QStringLiteral("WIP");

    // Spectral Index Information Tab
    m[QStringLiteral("guideTitle")] = ja
        ? QStringLiteral("解析方法と色の見方ガイド")
        : QStringLiteral("Spectral Index Information");
    m[QStringLiteral("guideSubtitle")] = ja
        ? QStringLiteral("右パネルのボタンと同じ順番で並んでいます。畑の生育段階や目的に合わせて選んでください。")
        : QStringLiteral("Ordered identically to the right-side method grid. Pick the method matching your crop stage and camera.");
    m[QStringLiteral("guideUseCaseLabel")] = ja ? QStringLiteral("使いどころ：") : QStringLiteral("When to use:");
    m[QStringLiteral("guideColorLabel")] = ja ? QStringLiteral("色の見方：") : QStringLiteral("What colors mean:");
    m[QStringLiteral("guideUseBtn")] = ja ? QStringLiteral("この方法で表示") : QStringLiteral("Use This Method");
    m[QStringLiteral("guideActiveBadge")] = ja ? QStringLiteral("選択中") : QStringLiteral("ACTIVE");
    m[QStringLiteral("guideLegendTitle")] = ja ? QStringLiteral("基本的な色分け（標準：赤黄緑）") : QStringLiteral("Standard Color Key (RdYlGn Palette)");
    m[QStringLiteral("guideLegendGreen")] = ja ? QStringLiteral("緑：良好・元気") : QStringLiteral("Green: Healthy / Dense");
    m[QStringLiteral("guideLegendYellow")] = ja ? QStringLiteral("黄：やや注意") : QStringLiteral("Yellow: Moderate / Watch");
    m[QStringLiteral("guideLegendRed")] = ja ? QStringLiteral("赤：ストレス・低下") : QStringLiteral("Red: Stressed / Low");
    m[QStringLiteral("guideLegendGray")] = ja ? QStringLiteral("濃い灰色：土・水・道・除外") : QStringLiteral("Dark Gray: Soil / Water / Excluded");
    m[QStringLiteral("guideScaleTip")] = ja
        ? QStringLiteral("下バーの「標準表示」が正しい生育基準での色分けです。畑全体が同じ色に見えて違いが分かりにくいときだけ「差を強調」を押すと、写真の中のわずかな差が見やすくなります（右の「健康状態」の割合は常に標準基準のまま変わりません）。")
        : QStringLiteral("Bottom Scale Bar: \"Standard\" shows true calibrated health benchmarks. Click \"Boost Contrast\" only when a field looks uniform and you want to highlight subtle within-photo differences (Crop Health Estimate % always stays anchored to true Standard benchmarks).");

    return m;
}

void AppController::setExecutionMode(int mode) {
    if (executionMode_ == mode) return;
    executionMode_ = mode;
    emit uiStateChanged();
}

void AppController::setSaasEndpoint(const QString& url) {
    if (saasEndpoint_ == url) return;
    saasEndpoint_ = url.trimmed();
    emit uiStateChanged();
}

void AppController::setViewMode(int mode) {
    if (viewMode_ == mode) return;
    viewMode_ = mode;
    emit uiStateChanged();
}

void AppController::setOverlayOpacity(double alpha) {
    const double clamped = std::clamp(alpha, 0.0, 1.0);
    if (std::fabs(overlayOpacity_ - clamped) < 1e-4) return;
    overlayOpacity_ = clamped;
    emit uiStateChanged();
}

void AppController::setSplitRatio(double ratio) {
    const double clamped = std::clamp(ratio, 0.05, 0.95);
    if (std::fabs(splitRatio_ - clamped) < 1e-4) return;
    splitRatio_ = clamped;
    emit uiStateChanged();
}

void AppController::setSelectedCaptureIndex(int idx) {
    if (idx < 0 || idx >= captureModel_->rowCount()) return;
    if (!usingSyntheticSample_ && selectedCaptureIndex_ == idx) return;

    usingSyntheticSample_ = false;
    selectedCaptureIndex_ = idx;
    if (const auto* cap = captureModel_->captureAt(idx)) {
        config_.sensorProfile = cap->sensorProfile;
    }
    emit dataChanged();
    emit configChanged();
    reprocess(true);
}

void AppController::setSensorProfile(int v) {
    const auto next = static_cast<agrovis::SensorProfile>(std::clamp(v, 0, 2));
    if (config_.sensorProfile == next) return;
    config_.sensorProfile = next;
    emit configChanged();
    reprocess();
}

void AppController::setSpectralIndex(int v) {
    const auto& specs = agrovis::SpectralEngine::allIndexSpecs();
    if (v < 0 || v >= static_cast<int>(specs.size())) return;
    const auto next = specs[static_cast<size_t>(v)].id;
    if (config_.index == next) return;
    config_.index = next;
    if (!autoRange_) {
        const auto& spec = agrovis::SpectralEngine::specFor(next);
        config_.clampMin = spec.defaultMin;
        config_.clampMax = spec.defaultMax;
    }
    emit configChanged();
    reprocess();
}

void AppController::setAlignmentMode(int v) {
    const auto next = static_cast<agrovis::AlignmentMode>(std::clamp(v, 0, 2));
    if (config_.alignment == next) return;
    config_.alignment = next;
    emit configChanged();
    reprocess();
}

void AppController::setRadiometryMode(int v) {
    const auto next = static_cast<agrovis::RadiometryMode>(std::clamp(v, 0, 2));
    if (config_.radiometry == next) return;
    config_.radiometry = next;
    emit configChanged();
    reprocess();
}

void AppController::setColormapKind(int v) {
    const auto next = static_cast<agrovis::ColormapKind>(std::clamp(v, 0, 3));
    if (config_.colormap == next) return;
    config_.colormap = next;
    emit configChanged();
    reprocess();
}

void AppController::setMlModelMode(int v) {
    const auto next = static_cast<agrovis::MlModelMode>(std::clamp(v, 0, 3));
    if (config_.mlModel == next) return;
    config_.mlModel = next;
    emit configChanged();
    reprocess();
}

void AppController::setMaskSoil(bool v) {
    if (config_.maskSoil == v) return;
    config_.maskSoil = v;
    emit configChanged();
    reprocess();
}

void AppController::setMaskWater(bool v) {
    if (config_.maskWater == v) return;
    config_.maskWater = v;
    emit configChanged();
    reprocess();
}

void AppController::setMaskShadow(bool v) {
    if (config_.maskShadow == v) return;
    config_.maskShadow = v;
    emit configChanged();
    reprocess();
}

void AppController::setMaskSkyGlare(bool v) {
    if (config_.maskSkyGlare == v) return;
    config_.maskSkyGlare = v;
    emit configChanged();
    reprocess();
}

void AppController::setClampMin(double v) {
    const float next = static_cast<float>(v);
    if (!autoRange_ && std::fabs(config_.clampMin - next) < 1e-4f) return;
    autoRange_ = false;
    config_.clampMin = next;
    emit configChanged();
    reprocess();
}

void AppController::setClampMax(double v) {
    const float next = static_cast<float>(v);
    if (!autoRange_ && std::fabs(config_.clampMax - next) < 1e-4f) return;
    autoRange_ = false;
    config_.clampMax = next;
    emit configChanged();
    reprocess();
}

void AppController::setAutoRange(bool v) {
    if (!v) {
        resetIndexClampDefaults();
        return;
    }
    if (autoRange_) return;
    autoRange_ = true;
    emit configChanged();
    reprocess();
}

void AppController::setCustomFormula(const QString& expr) {
    const std::string s = expr.toStdString();
    if (config_.customFormula == s) return;
    config_.customFormula = s;
    emit configChanged();
    if (config_.index == agrovis::SpectralIndex::Custom) {
        reprocess();
    }
}

QVariantList AppController::indexMetaList() const {
    const bool ja = (language_ != QLatin1String("en"));
    QVariantList list;
    const auto& specs = agrovis::SpectralEngine::allIndexSpecs();
    for (int i = 0; i < static_cast<int>(specs.size()); ++i) {
        const auto& s = specs[static_cast<size_t>(i)];
        if (s.id == agrovis::SpectralIndex::Custom) continue;
        const auto& loc = localizedInfoFor(s.id);
        QVariantMap item;
        item[QStringLiteral("indexPos")] = i;
        item[QStringLiteral("orderNum")] = list.size() + 1;
        item[QStringLiteral("shortName")] = QString::fromUtf8(ja ? loc.jaShort : loc.enShort);
        item[QStringLiteral("acronym")] = QString::fromUtf8(loc.enShort);
        item[QStringLiteral("fullName")] = QString::fromUtf8(ja ? loc.jaFull : loc.enFull);
        item[QStringLiteral("useCase")] = QString::fromUtf8(ja ? loc.jaUseCase : loc.enUseCase);
        item[QStringLiteral("colorMeaning")] = QString::fromUtf8(ja ? loc.jaColorMeaning : loc.enColorMeaning);
        item[QStringLiteral("displayName")] = QString::fromStdString(s.displayName);
        item[QStringLiteral("formula")] = QString::fromStdString(s.formula);
        item[QStringLiteral("requiresRedEdge")] = s.requiresRedEdge;
        item[QStringLiteral("supported")] = true;
        list.append(item);
    }
    return list;
}

QString AppController::activeIndexName() const {
    const bool ja = (language_ != QLatin1String("en"));
    const auto& loc = localizedInfoFor(config_.index);
    return QString::fromUtf8(ja ? loc.jaShort : loc.enShort);
}

QString AppController::activeIndexFormula() const {
    if (config_.index == agrovis::SpectralIndex::Custom) {
        return QString::fromStdString(config_.customFormula);
    }
    return QString::fromStdString(agrovis::SpectralEngine::specFor(config_.index).formula);
}

QString AppController::sensorCapabilitySummary() const {
    switch (config_.sensorProfile) {
        case agrovis::SensorProfile::DjiMavic3M:
            return QStringLiteral("Bands: RGB + G(560) + R(650) + RE(730) + NIR(860)");
        case agrovis::SensorProfile::Pi5DualRgbNir850:
            return QStringLiteral("Bands: RGB + 850nm NoIR (RedEdge indices disabled)");
        case agrovis::SensorProfile::GenericMultiTiff:
            return QStringLiteral("Bands: Generic Multi-Band TIFF Stack");
    }
    return {};
}

QString AppController::statusNote() const {
    const bool ja = (language_ != QLatin1String("en"));
    if (usingSyntheticSample_) {
        return ja
            ? QStringLiteral("テスト用サンプル圃場を表示中（基準カラー板・ストレス区画含む）")
            : QStringLiteral("Sample plot active (with reference card & stress zone)");
    }
    const QString raw = QString::fromStdString(currentOutput_.summary.statusNote);
    if (!ja) {
        return raw;
    }
    if (raw.startsWith(QLatin1String("Radiometry + multi-band alignment"))) {
        return QStringLiteral("自動補正・バンド位置合わせ完了");
    }
    if (raw.startsWith(QLatin1String("Band alignment disabled"))) {
        return QStringLiteral("位置合わせなし（生画像）");
    }
    return raw;
}

QString AppController::activeCaptureSummary() const {
    const bool ja = (language_ != QLatin1String("en"));
    if (usingSyntheticSample_) {
        return ja
            ? QStringLiteral("サンプル圃場 · 720×480 · 5バンド")
            : QStringLiteral("Calibration Sample Plot · 720×480 · 5 Bands");
    }
    if (const auto* cap = captureModel_->captureAt(selectedCaptureIndex_)) {
        return QString::fromStdString(cap->groupId) + QStringLiteral(" · ") +
               QString::number(currentOutput_.width) + QStringLiteral("×") +
               QString::number(currentOutput_.height);
    }
    return ja ? QStringLiteral("画像が選択されていません") : QStringLiteral("No capture selected");
}

void AppController::loadSyntheticSample() {
    usingSyntheticSample_ = true;
    selectedCaptureIndex_ = -1;
    emit dataChanged();
    reprocess();
}

void AppController::loadFolder(const QUrl& folderUrl) {
    const QString path = folderUrl.isLocalFile() ? folderUrl.toLocalFile() : folderUrl.toString();
    if (path.isEmpty()) return;

    auto captures = agrovis::CaptureScanner::scanDirectory(path.toStdString());
    loadedFolder_ = QDir::toNativeSeparators(path);
    captureModel_->setCaptures(std::move(captures));

    if (captureModel_->rowCount() > 0) {
        usingSyntheticSample_ = false;
        selectedCaptureIndex_ = 0;
        if (const auto* cap = captureModel_->captureAt(0)) {
            config_.sensorProfile = cap->sensorProfile;
        }
        emit dataChanged();
        emit configChanged();
        reprocess(true);
    } else {
        usingSyntheticSample_ = true;
        selectedCaptureIndex_ = -1;
        emit dataChanged();
        reprocess();
    }
}

void AppController::resetIndexClampDefaults() {
    autoRange_ = false;
    const auto& spec = agrovis::SpectralEngine::specFor(config_.index);
    config_.clampMin = spec.defaultMin;
    config_.clampMax = spec.defaultMax;
    emit configChanged();
    reprocess();
}

QVariantMap AppController::probeAtPixel(int x, int y) const {
    const auto p = agrovis::SpectralEngine::probePixel(currentOutput_, x, y);
    QVariantMap out;
    out[QStringLiteral("valid")] = p.valid;
    out[QStringLiteral("masked")] = p.masked;
    out[QStringLiteral("x")] = p.x;
    out[QStringLiteral("y")] = p.y;
    out[QStringLiteral("blue")] = p.blue;
    out[QStringLiteral("green")] = p.green;
    out[QStringLiteral("red")] = p.red;
    out[QStringLiteral("redEdge")] = p.redEdge;
    out[QStringLiteral("nir")] = p.nir;
    out[QStringLiteral("indexValue")] = p.indexValue;
    return out;
}

bool AppController::exportOverlayPng(const QUrl& fileUrl) const {
    QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    if (path.isEmpty()) return false;
    if (!path.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive)) {
        path += QStringLiteral(".png");
    }
    const QImage overlay = toQImage(currentOutput_.indexOverlay);
    if (overlay.isNull()) return false;
    return overlay.save(path, "PNG");
}

void AppController::reprocess(bool showLoadingImmediately) {
    const uint64_t seq = activeJobSeq_->fetch_add(1) + 1;
    const auto seqRef = activeJobSeq_;

    if (showLoadingImmediately) {
        loadingDelayTimer_.stop();
        if (!isLoading_) {
            isLoading_ = true;
            emit loadingChanged();
        }
    } else if (!isLoading_) {
        // Avoid flickering "Loading..." on <75 ms cache-hit index switches.
        loadingDelayTimer_.start(75);
    }

    const bool useSynthetic = usingSyntheticSample_ || selectedCaptureIndex_ < 0 ||
                              selectedCaptureIndex_ >= captureModel_->rowCount();
    agrovis::CaptureGroup capCopy;
    if (!useSynthetic) {
        if (const auto* cap = captureModel_->captureAt(selectedCaptureIndex_)) {
            capCopy = *cap;
        }
    }
    agrovis::ProcessingConfig cfgCopy = config_;
    const bool autoFit = autoRange_;
    QPointer<AppController> self(this);

    QThreadPool::globalInstance()->start(
        [self, seq, seqRef, useSynthetic, capCopy = std::move(capCopy), cfgCopy, autoFit]() mutable {
            if (seq != seqRef->load()) {
                return;
            }

            agrovis::WorkbenchOutput out = useSynthetic
                ? agrovis::SpectralEngine::processSyntheticSample(cfgCopy)
                : agrovis::SpectralEngine::processCaptureGroup(capCopy, cfgCopy);

            if (seq != seqRef->load()) {
                return;
            }

            if (autoFit && cfgCopy.index != agrovis::SpectralIndex::RGB) {
                computeAutoFitBounds(out, cfgCopy.clampMin, cfgCopy.clampMax);
                out = useSynthetic
                    ? agrovis::SpectralEngine::processSyntheticSample(cfgCopy)
                    : agrovis::SpectralEngine::processCaptureGroup(capCopy, cfgCopy);
            }

            const float appliedMin = cfgCopy.clampMin;
            const float appliedMax = cfgCopy.clampMax;
            QImage baseImg = toQImage(out.baseRgb);
            QImage overlayImg = toQImage(out.indexOverlay);

            if (self) {
                QMetaObject::invokeMethod(
                    self.data(),
                    [self, seq, out = std::move(out), baseImg = std::move(baseImg),
                     overlayImg = std::move(overlayImg), autoFit, appliedMin, appliedMax]() mutable {
                        if (!self || seq != self->activeJobSeq_->load()) {
                            return;
                        }
                        self->loadingDelayTimer_.stop();
                        self->currentOutput_ = std::move(out);
                        self->imageProvider_->setImages(std::move(baseImg), std::move(overlayImg));
                        ++self->imageRevision_;
                        if (autoFit) {
                            const bool rangeMoved =
                                (std::fabs(self->config_.clampMin - appliedMin) > 1e-4f) ||
                                (std::fabs(self->config_.clampMax - appliedMax) > 1e-4f);
                            self->config_.clampMin = appliedMin;
                            self->config_.clampMax = appliedMax;
                            if (rangeMoved) {
                                emit self->configChanged();
                            }
                        }
                        if (self->isLoading_) {
                            self->isLoading_ = false;
                            emit self->loadingChanged();
                        }
                        emit self->outputUpdated();
                    },
                    Qt::QueuedConnection);
            }
        });
}

}  // namespace agrovis::app
