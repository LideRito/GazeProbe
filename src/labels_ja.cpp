#include "labels_ja.h"

static const char* const LABELS[ChannelCount] = {"明暗","色の対比", "彩度", "顔", "中央寄り"};

static const char* const HINTS[ChannelCount] = {
    "周囲と比べて明度がどれだけ変化しているか。輪郭や描き込みの密な所ほど強くなります。",
    "周囲の平均的な色から、色相や彩度がどれだけ離れているか。",
    "色そのものの鮮やかさ。周囲に関係なく、鮮やかな色ほど強くなります。",
    "検出した顔の位置。人は絵の中の顔をまず見ます。",
    "画面の中央ほど強くなります。人は画面の中央から見始める傾向があります。",
};

const char* ChannelLabel(Channel c) {
    return LABELS[c];
}

const char* ChannelHint(Channel c) {
    return HINTS[c];
}

const char* ImageStatusText(ImageStatus status) {
    switch (status) {
    case ImageStatus::Ready: return "正常";
    case ImageStatus::Unavailable: return "この環境では WIC（Windows の画像機能）を利用できません";
    case ImageStatus::OpenFailed: return "画像を開けませんでした";
    case ImageStatus::DecodeFailed: return "画像を読み込めませんでした";
    case ImageStatus::ResizeFailed: return "画像を縮小できませんでした";
    case ImageStatus::WriteFailed: return "画像を書き出せませんでした";
    }
    return "不明";
}

const char* FaceStatusText(FaceStatus status) {
    switch (status) {
    case FaceStatus::Ready: return "正常";
    case FaceStatus::ModelMissing: return "モデルが見つかりません";
    case FaceStatus::LoadFailed: return "モデルを読み込めませんでした";
    case FaceStatus::DetectFailed: return "検出に失敗しました";
    }
    return "不明";
}
