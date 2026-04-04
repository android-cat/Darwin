#!/bin/bash
# Darwin DAW - macOS 配布パッケージ生成スクリプト

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$SCRIPT_DIR"

APP_NAME="Darwin"
BUILD_DIR="$REPO_ROOT/build"
OUTPUT_DIR=""
BUNDLE_ID="com.android-cat.darwin"
VERSION=""
SKIP_BUILD=0
CREATE_DMG=1

SOURCE_ICON_PNG="$REPO_ROOT/icons/darwin.png"
ENTITLEMENTS_FILE="$REPO_ROOT/packaging/macos/Darwin.entitlements"

MACDEPLOYQT_EXECUTABLE="${DARWIN_MACDEPLOYQT:-}"
CODESIGN_IDENTITY="${DARWIN_CODESIGN_IDENTITY:-}"
NOTARY_PROFILE="${DARWIN_NOTARY_PROFILE:-}"
APPLE_ID="${DARWIN_APPLE_ID:-}"
APPLE_PASSWORD="${DARWIN_APPLE_PASSWORD:-}"
TEAM_ID="${DARWIN_TEAM_ID:-}"
EFFECTIVE_CODESIGN_IDENTITY=""

usage() {
    cat <<'EOF'
使用方法:
  ./package_macos.sh [options]

主なオプション:
  --build-dir <dir>            ビルドディレクトリ (既定: ./build)
  --output-dir <dir>           配布物出力先 (既定: <build-dir>/dist-macos)
  --app-name <name>            アプリ名 (既定: Darwin)
  --bundle-id <id>             CFBundleIdentifier (既定: com.android-cat.darwin)
  --version <version>          配布物バージョン (既定: CMakeLists.txt から自動取得)
  --icon-png <path>            .icns 生成元 PNG (既定: icons/darwin.png)
  --codesign-identity <name>   Developer ID 用 codesign identity
  --notary-profile <name>      notarytool の keychain profile 名
  --apple-id <mail>            notarytool 用 Apple ID
  --apple-password <password>  notarytool 用 app-specific password
  --team-id <id>               notarytool 用 Team ID
  --macdeployqt <path>         macdeployqt のフルパス
  --skip-build                 既存 build/Darwin をそのまま使う
  --no-dmg                     dmg を作らず .app のみ生成する
  --help                       このヘルプを表示

環境変数:
  DARWIN_MACDEPLOYQT
  DARWIN_CODESIGN_IDENTITY
  DARWIN_NOTARY_PROFILE
  DARWIN_APPLE_ID
  DARWIN_APPLE_PASSWORD
  DARWIN_TEAM_ID

例:
  ./package_macos.sh
  ./package_macos.sh --codesign-identity "Developer ID Application: Example Corp"
  ./package_macos.sh --codesign-identity "Developer ID Application: Example Corp" \
      --notary-profile darwin-notary
EOF
}

log_info() {
    printf '[INFO] %s\n' "$*"
}

log_warn() {
    printf '[WARN] %s\n' "$*" >&2
}

die() {
    printf '[ERROR] %s\n' "$*" >&2
    exit 1
}

require_command() {
    local cmd="$1"
    command -v "$cmd" >/dev/null 2>&1 || die "$cmd が見つからないわ。先にインストールしなさいよね。"
}

parse_args() {
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --build-dir)
                BUILD_DIR="$2"
                shift 2
                ;;
            --output-dir)
                OUTPUT_DIR="$2"
                shift 2
                ;;
            --app-name)
                APP_NAME="$2"
                shift 2
                ;;
            --bundle-id)
                BUNDLE_ID="$2"
                shift 2
                ;;
            --version)
                VERSION="$2"
                shift 2
                ;;
            --icon-png)
                SOURCE_ICON_PNG="$2"
                shift 2
                ;;
            --codesign-identity)
                CODESIGN_IDENTITY="$2"
                shift 2
                ;;
            --notary-profile)
                NOTARY_PROFILE="$2"
                shift 2
                ;;
            --apple-id)
                APPLE_ID="$2"
                shift 2
                ;;
            --apple-password)
                APPLE_PASSWORD="$2"
                shift 2
                ;;
            --team-id)
                TEAM_ID="$2"
                shift 2
                ;;
            --macdeployqt)
                MACDEPLOYQT_EXECUTABLE="$2"
                shift 2
                ;;
            --skip-build)
                SKIP_BUILD=1
                shift
                ;;
            --no-dmg)
                CREATE_DMG=0
                shift
                ;;
            --help|-h)
                usage
                exit 0
                ;;
            *)
                die "知らないオプションよ: $1"
                ;;
        esac
    done
}

resolve_version() {
    if [[ -n "$VERSION" ]]; then
        return
    fi

    VERSION="$(sed -nE 's/^project\(Darwin VERSION ([^ )]+).*/\1/p' "$REPO_ROOT/CMakeLists.txt" | head -n 1)"
    if [[ -z "$VERSION" ]]; then
        VERSION="0.0.0"
        log_warn "CMakeLists.txt からバージョンを拾えなかったから、仮で $VERSION を使うわ。"
    fi
}

resolve_output_dir() {
    if [[ -z "$OUTPUT_DIR" ]]; then
        OUTPUT_DIR="$BUILD_DIR/dist-macos"
    fi
}

resolve_codesign_identity() {
    if [[ -n "$CODESIGN_IDENTITY" ]]; then
        EFFECTIVE_CODESIGN_IDENTITY="$CODESIGN_IDENTITY"
        return
    fi

    EFFECTIVE_CODESIGN_IDENTITY="-"
}

find_macdeployqt() {
    if [[ -n "$MACDEPLOYQT_EXECUTABLE" ]]; then
        [[ -x "$MACDEPLOYQT_EXECUTABLE" ]] || die "指定された macdeployqt が実行できないわ: $MACDEPLOYQT_EXECUTABLE"
        return
    fi

    if command -v macdeployqt >/dev/null 2>&1; then
        MACDEPLOYQT_EXECUTABLE="$(command -v macdeployqt)"
        return
    fi

    if command -v qmake >/dev/null 2>&1; then
        local qt_bin_dir=""
        qt_bin_dir="$(qmake -query QT_INSTALL_BINS 2>/dev/null || true)"
        if [[ -n "$qt_bin_dir" && -x "$qt_bin_dir/macdeployqt" ]]; then
            MACDEPLOYQT_EXECUTABLE="$qt_bin_dir/macdeployqt"
            return
        fi
    fi

    if command -v qmake6 >/dev/null 2>&1; then
        local qt6_bin_dir=""
        qt6_bin_dir="$(qmake6 -query QT_INSTALL_BINS 2>/dev/null || true)"
        if [[ -n "$qt6_bin_dir" && -x "$qt6_bin_dir/macdeployqt" ]]; then
            MACDEPLOYQT_EXECUTABLE="$qt6_bin_dir/macdeployqt"
            return
        fi
    fi

    if [[ -n "${CMAKE_PREFIX_PATH:-}" ]]; then
        local prefix=""
        local candidate=""
        local old_ifs="$IFS"
        IFS=':'
        for prefix in $CMAKE_PREFIX_PATH; do
            candidate="$prefix/bin/macdeployqt"
            if [[ -x "$candidate" ]]; then
                MACDEPLOYQT_EXECUTABLE="$candidate"
                IFS="$old_ifs"
                return
            fi

            candidate="$(cd "$prefix/.." 2>/dev/null && pwd)/bin/macdeployqt"
            if [[ -x "$candidate" ]]; then
                MACDEPLOYQT_EXECUTABLE="$candidate"
                IFS="$old_ifs"
                return
            fi
        done
        IFS="$old_ifs"
    fi

    die "macdeployqt が見つからないわ。PATH か DARWIN_MACDEPLOYQT で教えなさい。"
}

configure_and_build() {
    if [[ "$SKIP_BUILD" -eq 1 ]]; then
        log_info "既存ビルドを使うから、ビルド工程は飛ばすわ。"
        return
    fi

    require_command cmake

    local generator="Ninja"
    if ! command -v ninja >/dev/null 2>&1; then
        generator="Unix Makefiles"
    fi

    if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
        log_info "Release ビルドを構成するわ。"
        if [[ -n "${CMAKE_PREFIX_PATH:-}" ]]; then
            cmake -S "$REPO_ROOT" -B "$BUILD_DIR" -G "$generator" \
                -DCMAKE_BUILD_TYPE=Release \
                -DCMAKE_PREFIX_PATH="$CMAKE_PREFIX_PATH"
        else
            cmake -S "$REPO_ROOT" -B "$BUILD_DIR" -G "$generator" \
                -DCMAKE_BUILD_TYPE=Release
        fi
    fi

    log_info "Release ビルドを実行するわ。"
    cmake --build "$BUILD_DIR" --config Release
}

generate_info_plist() {
    local plist_path="$1"

    cat > "$plist_path" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key>
    <string>en</string>
    <key>CFBundleDisplayName</key>
    <string>${APP_NAME}</string>
    <key>CFBundleExecutable</key>
    <string>${APP_NAME}</string>
    <key>CFBundleIconFile</key>
    <string>${APP_NAME}.icns</string>
    <key>CFBundleIdentifier</key>
    <string>${BUNDLE_ID}</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>CFBundleName</key>
    <string>${APP_NAME}</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>${VERSION}</string>
    <key>CFBundleVersion</key>
    <string>${VERSION}</string>
    <key>LSApplicationCategoryType</key>
    <string>public.app-category.music</string>
    <key>LSMinimumSystemVersion</key>
    <string>13.0</string>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>NSMicrophoneUsageDescription</key>
    <string>Darwin uses audio input for recording and monitoring.</string>
</dict>
</plist>
EOF
}

generate_icns() {
    local output_icns="$1"

    if [[ ! -f "$SOURCE_ICON_PNG" ]]; then
        log_warn "アイコン元 PNG が見つからないから、.icns 生成は飛ばすわ: $SOURCE_ICON_PNG"
        return
    fi

    if ! command -v sips >/dev/null 2>&1 || ! command -v iconutil >/dev/null 2>&1; then
        log_warn "sips か iconutil が無いから、.icns 生成は飛ばすわ。"
        return
    fi

    local temp_root=""
    local iconset_dir=""
    temp_root="$(mktemp -d "${TMPDIR:-/tmp}/darwin-icon.XXXXXX")"
    iconset_dir="$temp_root/${APP_NAME}.iconset"
    mkdir -p "$iconset_dir"

    # 配布用アイコンの標準サイズ群をまとめて生成する。
    local size=""
    for size in 16 32 128 256 512; do
        sips -z "$size" "$size" "$SOURCE_ICON_PNG" \
            --out "$iconset_dir/icon_${size}x${size}.png" >/dev/null
        sips -z "$((size * 2))" "$((size * 2))" "$SOURCE_ICON_PNG" \
            --out "$iconset_dir/icon_${size}x${size}@2x.png" >/dev/null
    done

    iconutil -c icns "$iconset_dir" -o "$output_icns"
    rm -rf "$temp_root"
}

stage_app_bundle() {
    local binary_path="$BUILD_DIR/$APP_NAME"
    [[ -f "$binary_path" ]] || die "ビルド済み実行ファイルが無いわ: $binary_path"

    APP_BUNDLE_PATH="$OUTPUT_DIR/${APP_NAME}.app"
    APP_CONTENTS_PATH="$APP_BUNDLE_PATH/Contents"
    APP_MACOS_PATH="$APP_CONTENTS_PATH/MacOS"
    APP_RESOURCES_PATH="$APP_CONTENTS_PATH/Resources"
    APP_PLIST_PATH="$APP_CONTENTS_PATH/Info.plist"
    APP_BINARY_PATH="$APP_MACOS_PATH/$APP_NAME"
    APP_DMG_PATH="$OUTPUT_DIR/${APP_NAME}-${VERSION}-macOS.dmg"
    APP_ZIP_PATH="$OUTPUT_DIR/${APP_NAME}-${VERSION}-macOS.zip"

    rm -rf "$APP_BUNDLE_PATH"
    mkdir -p "$APP_MACOS_PATH" "$APP_RESOURCES_PATH"

    log_info ".app をステージングするわ。"
    cp "$binary_path" "$APP_BINARY_PATH"
    chmod +x "$APP_BINARY_PATH"

    generate_info_plist "$APP_PLIST_PATH"
    generate_icns "$APP_RESOURCES_PATH/${APP_NAME}.icns"
    cp "$REPO_ROOT/LICENSE" "$APP_RESOURCES_PATH/LICENSE"
}

deploy_qt_runtime() {
    log_info "macdeployqt で Qt ランタイムを収集するわ。"
    "$MACDEPLOYQT_EXECUTABLE" "$APP_BUNDLE_PATH" -always-overwrite -verbose=0
}

sign_items_in_dir() {
    local target_dir="$1"
    [[ -d "$target_dir" ]] || return

    while IFS= read -r component; do
        [[ -n "$component" ]] || continue
        log_info "署名: $component"
        if [[ "$EFFECTIVE_CODESIGN_IDENTITY" == "-" ]]; then
            codesign --force --sign "$EFFECTIVE_CODESIGN_IDENTITY" "$component"
        else
            codesign --force --sign "$EFFECTIVE_CODESIGN_IDENTITY" --timestamp "$component"
        fi
    done < <(
        find "$target_dir" -depth \
            \( -type d \( -name "*.framework" -o -name "*.bundle" -o -name "*.app" -o -name "*.xpc" \) \
            -o -type f \( -name "*.dylib" -o -name "*.so" \) \) -print
    )
}

codesign_app_bundle() {
    require_command codesign

    log_info "署名前に xattr を掃除するわ。"
    xattr -cr "$APP_BUNDLE_PATH" || true

    if [[ "$EFFECTIVE_CODESIGN_IDENTITY" == "-" ]]; then
        log_info "Developer ID が無いから、ad-hoc 署名でローカル起動できる形に整えるわ。"
    else
        [[ -f "$ENTITLEMENTS_FILE" ]] || die "entitlements ファイルが無いわ: $ENTITLEMENTS_FILE"
    fi

    sign_items_in_dir "$APP_BUNDLE_PATH/Contents/Frameworks"
    sign_items_in_dir "$APP_BUNDLE_PATH/Contents/PlugIns"

    log_info "メイン実行ファイルに署名するわ。"
    if [[ "$EFFECTIVE_CODESIGN_IDENTITY" == "-" ]]; then
        codesign --force --sign "$EFFECTIVE_CODESIGN_IDENTITY" "$APP_BINARY_PATH"
    else
        codesign --force --sign "$EFFECTIVE_CODESIGN_IDENTITY" --timestamp "$APP_BINARY_PATH"
    fi

    log_info "アプリバンドル本体に署名するわ。"
    if [[ "$EFFECTIVE_CODESIGN_IDENTITY" == "-" ]]; then
        # macdeployqt 後の linker-signed 状態を ad-hoc で上書きして、ローカル実行可能な形にする。
        codesign --force --sign "$EFFECTIVE_CODESIGN_IDENTITY" "$APP_BUNDLE_PATH"
    else
        codesign --force --sign "$EFFECTIVE_CODESIGN_IDENTITY" --timestamp \
            --options runtime \
            --entitlements "$ENTITLEMENTS_FILE" \
            "$APP_BUNDLE_PATH"
    fi

    log_info "署名検証を走らせるわ。"
    codesign --verify --deep --strict --verbose=2 "$APP_BUNDLE_PATH"
}

create_dmg_artifact() {
    if [[ "$CREATE_DMG" -eq 0 ]]; then
        return
    fi

    require_command hdiutil

    rm -f "$APP_DMG_PATH"
    log_info "配布用 dmg を作るわ。"
    hdiutil create \
        -volname "$APP_NAME" \
        -srcfolder "$APP_BUNDLE_PATH" \
        -ov \
        -format UDZO \
        "$APP_DMG_PATH" >/dev/null
}

create_zip_artifact() {
    rm -f "$APP_ZIP_PATH"
    log_info "notarize 用 zip を作るわ。"
    ditto -c -k --keepParent "$APP_BUNDLE_PATH" "$APP_ZIP_PATH"
}

should_notarize() {
    [[ -n "$NOTARY_PROFILE" || -n "$APPLE_ID" || -n "$APPLE_PASSWORD" || -n "$TEAM_ID" ]]
}

validate_notary_configuration() {
    if [[ -n "$NOTARY_PROFILE" ]]; then
        return
    fi

    [[ -n "$APPLE_ID" ]] || die "notarize するなら --apple-id か --notary-profile が必要よ。"
    [[ -n "$APPLE_PASSWORD" ]] || die "notarize するなら --apple-password も必要よ。"
    [[ -n "$TEAM_ID" ]] || die "notarize するなら --team-id も必要よ。"
}

notarize_artifact() {
    if ! should_notarize; then
        log_warn "notarize 情報が無いから、notarization は飛ばすわ。"
        return
    fi

    [[ -n "$CODESIGN_IDENTITY" ]] || die "notarize するなら先に codesign identity を指定しなさい。"

    require_command xcrun
    validate_notary_configuration

    local submit_target=""
    if [[ "$CREATE_DMG" -eq 1 ]]; then
        submit_target="$APP_DMG_PATH"
    else
        create_zip_artifact
        submit_target="$APP_ZIP_PATH"
    fi

    local cmd=(xcrun notarytool submit "$submit_target" --wait)
    if [[ -n "$NOTARY_PROFILE" ]]; then
        cmd+=(--keychain-profile "$NOTARY_PROFILE")
    else
        cmd+=(--apple-id "$APPLE_ID" --password "$APPLE_PASSWORD" --team-id "$TEAM_ID")
    fi

    log_info "notarytool へ提出するわ。少し待ちなさい。"
    "${cmd[@]}"

    log_info "staple を付けるわ。"
    xcrun stapler staple "$APP_BUNDLE_PATH"
    if [[ "$CREATE_DMG" -eq 1 && -f "$APP_DMG_PATH" ]]; then
        xcrun stapler staple "$APP_DMG_PATH"
    fi
}

print_summary() {
    printf '\n'
    log_info "macOS 配布物の生成が終わったわ。"
    printf '  .app: %s\n' "$APP_BUNDLE_PATH"
    if [[ "$CREATE_DMG" -eq 1 && -f "$APP_DMG_PATH" ]]; then
        printf '  .dmg: %s\n' "$APP_DMG_PATH"
    fi
    if [[ -f "$APP_ZIP_PATH" ]]; then
        printf '  .zip: %s\n' "$APP_ZIP_PATH"
    fi
}

main() {
    [[ "$(uname -s)" == "Darwin" ]] || die "このスクリプトは macOS 専用よ。"

    parse_args "$@"
    resolve_version
    resolve_output_dir
    resolve_codesign_identity
    find_macdeployqt
    configure_and_build

    mkdir -p "$OUTPUT_DIR"

    stage_app_bundle
    deploy_qt_runtime
    codesign_app_bundle
    create_dmg_artifact
    notarize_artifact
    print_summary
}

APP_BUNDLE_PATH=""
APP_CONTENTS_PATH=""
APP_MACOS_PATH=""
APP_RESOURCES_PATH=""
APP_PLIST_PATH=""
APP_BINARY_PATH=""
APP_DMG_PATH=""
APP_ZIP_PATH=""

main "$@"
