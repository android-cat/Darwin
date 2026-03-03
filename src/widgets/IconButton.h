#pragma once

#include <QPushButton>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QVariantAnimation>
#include "common/ThemeManager.h"

/**
 * @brief 24x24 アイコンボタン（Eye / Trash）
 *
 * ArrangementView のトラックヘッダーで使用される小型アイコンボタン。
 * Eye アイコンは checked=true で開眼、false でまぶたを閉じるアニメーション。
 */
class IconButton : public QPushButton {
    Q_OBJECT
public:
    enum IconType { Eye, Trash, Folder, Chevron, Magnet };

    explicit IconButton(IconType type, QWidget* parent = nullptr)
        : QPushButton("", parent), m_type(type)
    {
        setFixedSize(24, 24);
        setCursor(Qt::PointingHandCursor);

        // テーマ変更時に再描画
        connect(&Darwin::ThemeManager::instance(), &Darwin::ThemeManager::themeChanged,
                this, [this]() { update(); });

        if (m_type == Eye) {
            m_eyeOpenness = 1.0f;
            connect(this, &QPushButton::toggled, this, &IconButton::onEyeToggled);
        } else if (m_type == Chevron) {
            m_chevronRotation = 0.0f; 
            connect(this, &QPushButton::toggled, this, &IconButton::onChevronToggled);
        } else if (m_type == Magnet) {
            setCheckable(true);
            setChecked(true); // デフォルトON
            m_magnetFieldPulse = 1.0f;
            connect(this, &QPushButton::toggled, this, &IconButton::onMagnetToggled);
        }
    }

    void setInitialFolderState(bool expanded) {
        if (m_type == Folder) {
            setChecked(expanded);
            update();
        } else if (m_type == Chevron) {
            setChecked(expanded);
            m_chevronRotation = expanded ? 90.0f : 0.0f;
            update();
        }
    }

protected:
    void paintEvent(QPaintEvent* event) override {
        QPushButton::paintEvent(event);

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        QColor color = QColor("#64748b");
        const bool isDark = Darwin::ThemeManager::instance().isDarkMode();
        const QColor hoverText = isDark ? QColor("#e2e8f0") : QColor("#1e293b");
        if (m_type == Eye) {
            if (underMouse()) color = QColor("#ef4444");
        } else if (m_type == Trash) {
            if (underMouse()) color = QColor("#ef4444");
        } else if (m_type == Folder) {
            color = QColor("#64748b");
            if (underMouse()) color = hoverText;
        } else if (m_type == Chevron) {
            if (underMouse()) color = hoverText;
        } else if (m_type == Magnet) {
            if (isChecked()) color = QColor("#FF3366");
            else if (underMouse()) color = hoverText;
        }

        p.setPen(QPen(color, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));

        if (m_type == Eye) {
            drawEyeIcon(p, color);
        } else if (m_type == Trash) {
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(7, 8, 10, 11, 1, 1);
            p.drawLine(5, 8, 19, 8);
            p.drawRoundedRect(10, 5, 4, 3, 1, 1);
            p.drawLine(10, 11, 10, 15);
            p.drawLine(14, 11, 14, 15);
        } else if (m_type == Folder) {
            p.setBrush(Qt::NoBrush);
            if (isChecked()) {
                // Open folder
                QPainterPath back;
                back.moveTo(4, 11);
                back.lineTo(4, 6);
                back.lineTo(9, 6);
                back.lineTo(11, 8);
                back.lineTo(20, 8);
                back.lineTo(20, 11);
                p.drawPath(back);

                QPainterPath flap;
                flap.moveTo(2, 11);
                flap.lineTo(19, 11);
                flap.lineTo(21, 18);
                flap.lineTo(4, 18);
                flap.closeSubpath();
                p.drawPath(flap);
            } else {
                // Closed folder
                QPainterPath path;
                path.moveTo(4, 6);
                path.lineTo(9, 6);
                path.lineTo(11, 8);
                path.lineTo(20, 8);
                path.lineTo(20, 18);
                path.lineTo(4, 18);
                path.closeSubpath();
                p.drawPath(path);
                p.drawLine(4, 10, 20, 10);
            }
        } else if (m_type == Chevron) {
            p.setBrush(Qt::NoBrush);
            p.translate(12, 12);
            p.rotate(m_chevronRotation);
            p.translate(-12, -12);
            
            QPainterPath path;
            path.moveTo(10, 8);
            path.lineTo(14, 12);
            path.lineTo(10, 16);
            p.drawPath(path);
            p.resetTransform();
        } else if (m_type == Magnet) {
            drawMagnetIcon(p, color);
        }
    }

private:
    void drawEyeIcon(QPainter& p, const QColor& color) {
        // m_eyeOpenness: 1.0 = fully open, 0.0 = fully closed
        float t = m_eyeOpenness;

        // 上まぶた制御点 Y: 開→4, 閉→12（中心線）
        float upperCpY = 12.0f - 8.0f * t;   // 4 when open, 12 when closed
        // 下まぶた制御点 Y: 開→20, 閉→12（中心線）
        float lowerCpY = 12.0f + 8.0f * t;   // 20 when open, 12 when closed

        // 目の輪郭（上まぶた + 下まぶた）
        p.setBrush(Qt::NoBrush);
        QPainterPath eyePath;
        eyePath.moveTo(3, 12);
        eyePath.quadTo(12, upperCpY, 21, 12);
        eyePath.quadTo(12, lowerCpY, 3, 12);
        p.drawPath(eyePath);

        // 瞳（開き具合に応じてサイズ・不透明度を変化）
        if (t > 0.15f) {
            float pupilSize = 2.0f * t;
            float pupilAlpha = qBound(0.0f, (t - 0.15f) / 0.35f, 1.0f);
            QColor pupilColor = color;
            pupilColor.setAlphaF(pupilAlpha);
            p.setBrush(pupilColor);
            p.setPen(Qt::NoPen);
            p.drawEllipse(QPointF(12, 12), pupilSize, pupilSize);
            p.setPen(QPen(color, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        }

        // まつ毛（常に表示 — 開眼時は上まぶたから上向き、閉眼時は中心線から下向き）
        {
            QColor lashColor = color;
            lashColor.setAlphaF(0.8f);
            p.setPen(QPen(lashColor, 1.2, Qt::SolidLine, Qt::RoundCap));

            // 上まぶたのカーブ上の3点 (x=8,12,16) での Y座標を算出
            // 二次ベジエ: P = (1-s)^2*P0 + 2*(1-s)*s*CP + s^2*P1
            // P0=(3,12), CP=(12,upperCpY), P1=(21,12)
            auto upperLidY = [&](float x) -> float {
                // x=3→s=0, x=21→s=1
                float s = (x - 3.0f) / 18.0f;
                float y = (1-s)*(1-s)*12.0f + 2*(1-s)*s*upperCpY + s*s*12.0f;
                return y;
            };

            // まつ毛の方向: 開いている時は上向き(-Y)、閉じている時は下向き(+Y)
            // t=1(open): direction=-1, t=0(closed): direction=+1
            float dir = 1.0f - 2.0f * t;  // 1→-1 (open=up), 0→+1 (closed=down)
            float lashLen = 2.5f + 0.5f * t;  // 開いているときやや長め

            // 左まつ毛
            float ly = upperLidY(8);
            p.drawLine(QPointF(8, ly), QPointF(6.5f, ly + dir * lashLen));
            // 中央まつ毛
            float cy = upperLidY(12);
            p.drawLine(QPointF(12, cy), QPointF(12, cy + dir * (lashLen + 0.5f)));
            // 右まつ毛
            float ry = upperLidY(16);
            p.drawLine(QPointF(16, ry), QPointF(17.5f, ry + dir * lashLen));
        }
    }

    void drawMagnetIcon(QPainter& p, const QColor& color) {
        p.setRenderHint(QPainter::Antialiasing);

        // --- 磁界の演出 (吸い込みアニメーション) ---
        // ONになる時: t が 0.0 -> 1.0 に変わり、大きく描いた波が中心(ポール)へ向かって縮小・吸い込まれる
        // OFFになる時: t が 1.0 -> 0.0 に変わり、中心から外側へ波が解放される
        if (m_magnetFieldPulse > 0.0f && m_magnetFieldPulse < 1.0f) {
            p.save();
            QColor fieldColor = color;
            float t = m_magnetFieldPulse;

            // t に対して縮小する2つの波を生成
            float s1 = 2.4f - 1.8f * t; 
            float s2 = 3.0f - 1.8f * t; 

            auto drawWave = [&](float s, float alphaBase) {
                if (s <= 0.4f) return;
                
                // フェードイン・アウト処理
                float alpha = 0.0f;
                if (t < 0.2f) alpha = t / 0.2f;
                else if (t > 0.8f) alpha = (1.0f - t) / 0.2f;
                else alpha = 1.0f;
                alpha *= alphaBase;

                fieldColor.setAlphaF(0.9f * alpha);
                QPen fieldPen(fieldColor, 1.5, Qt::DashLine, Qt::RoundCap);
                QVector<qreal> dashes;
                dashes << 2 << 3; 
                fieldPen.setDashPattern(dashes);
                p.setPen(fieldPen);
                p.setBrush(Qt::NoBrush);

                p.save();
                p.translate(12, 5);
                p.scale(s, s);
                p.translate(-12, -5);
                // 磁石上部へ広がるアーチ
                p.drawArc(QRectF(4, -3, 16, 16), 0, 180 * 16);
                p.restore();
            };

            drawWave(s1, 1.0f);
            drawWave(s2, 0.7f);
            p.restore();
        }

        // --- U字磁石本体 ---
        QPainterPath strokePath;
        strokePath.moveTo(6, 5);
        strokePath.lineTo(6, 12);
        // 底のアーチ
        strokePath.arcTo(6, 6, 12, 12, 180, 180);
        strokePath.lineTo(18, 5);

        // U字本体の描画 (太い線)
        p.setPen(QPen(color, 4.0, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin));
        p.setBrush(Qt::NoBrush);
        p.drawPath(strokePath);

        // === ポールの境界（先端キャップ）の表現 ===
        // 先端を分離して見せるための隙間線
        p.setPen(QPen(QColor(255, 255, 255, 200), 1.5));
        p.drawLine(4, 9, 8, 9);
        p.drawLine(16, 9, 20, 9);

        // === ON状態時の静的な磁力線（吸い寄せた状態の維持） ===
        if (isChecked()) {
            p.setPen(QPen(color.lighter(130), 1.2, Qt::DotLine, Qt::RoundCap));
            p.setBrush(Qt::NoBrush);
            // 両極間でループしているような小さなアーチを描き、常にONであることがわかるようにする
            p.drawArc(QRectF(8, 2, 8, 5), 0, 180 * 16);
            // 反対向き（下膨れ）の小さなアーチ
            p.drawArc(QRectF(8.5, 3.5, 7, 4), 180 * 16, 180 * 16); 
        }
    }

    void onEyeToggled(bool checked) {
        // アニメーション: 開閉をスムーズに補間
        if (m_eyeAnim) {
            m_eyeAnim->stop();
            delete m_eyeAnim;
            m_eyeAnim = nullptr;
        }
        m_eyeAnim = new QVariantAnimation(this);
        m_eyeAnim->setStartValue(static_cast<double>(m_eyeOpenness));
        m_eyeAnim->setEndValue(checked ? 1.0 : 0.0);
        m_eyeAnim->setDuration(220);
        m_eyeAnim->setEasingCurve(checked ? QEasingCurve::OutBack : QEasingCurve::InQuad);
        connect(m_eyeAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& val) {
            m_eyeOpenness = static_cast<float>(val.toDouble());
            update();
        });
        connect(m_eyeAnim, &QVariantAnimation::finished, this, [this]() {
            m_eyeAnim = nullptr;
        });
        m_eyeAnim->start(QAbstractAnimation::DeleteWhenStopped);
    }

    void onChevronToggled(bool checked) {
        if (m_chevronAnim) {
            m_chevronAnim->stop();
            delete m_chevronAnim;
            m_chevronAnim = nullptr;
        }
        m_chevronAnim = new QVariantAnimation(this);
        m_chevronAnim->setStartValue(static_cast<double>(m_chevronRotation));
        m_chevronAnim->setEndValue(checked ? 90.0 : 0.0); // checked = true のとき下を向く
        m_chevronAnim->setDuration(200);
        m_chevronAnim->setEasingCurve(QEasingCurve::InOutQuad);
        connect(m_chevronAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& val) {
            m_chevronRotation = static_cast<float>(val.toDouble());
            update();
        });
        connect(m_chevronAnim, &QVariantAnimation::finished, this, [this]() {
            m_chevronAnim = nullptr;
        });
        m_chevronAnim->start(QAbstractAnimation::DeleteWhenStopped);
    }

    void onMagnetToggled(bool checked) {
        if (m_magnetAnim) {
            m_magnetAnim->stop();
            delete m_magnetAnim;
            m_magnetAnim = nullptr;
        }
        m_magnetAnim = new QVariantAnimation(this);
        m_magnetAnim->setStartValue(static_cast<double>(m_magnetFieldPulse));
        m_magnetAnim->setEndValue(checked ? 1.0 : 0.0);
        m_magnetAnim->setDuration(450);
        m_magnetAnim->setEasingCurve(checked ? QEasingCurve::InCubic : QEasingCurve::OutCubic);
        connect(m_magnetAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& val) {
            m_magnetFieldPulse = static_cast<float>(val.toDouble());
            update();
        });
        connect(m_magnetAnim, &QVariantAnimation::finished, this, [this]() {
            m_magnetAnim = nullptr;
            update();
        });
        m_magnetAnim->start(QAbstractAnimation::DeleteWhenStopped);
    }

    IconType m_type;
    float m_eyeOpenness = 1.0f;
    float m_chevronRotation = 0.0f;
    float m_magnetFieldPulse = 1.0f;
    QVariantAnimation* m_eyeAnim = nullptr;
    QVariantAnimation* m_chevronAnim = nullptr;
    QVariantAnimation* m_magnetAnim = nullptr;
};
