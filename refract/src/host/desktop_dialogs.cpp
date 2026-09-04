#include "desktop_dialogs.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QFont>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLibraryInfo>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPushButton>
#include <QVariantAnimation>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <mutex>

namespace refract::desktop {
namespace {

constexpr std::uint32_t psp_start = 0x000008U;
constexpr std::uint32_t psp_up = 0x000010U;
constexpr std::uint32_t psp_right = 0x000020U;
constexpr std::uint32_t psp_down = 0x000040U;
constexpr std::uint32_t psp_left = 0x000080U;
constexpr std::uint32_t psp_circle = 0x002000U;
constexpr std::uint32_t psp_cross = 0x004000U;
constexpr std::uint32_t psp_square = 0x008000U;

constexpr std::array<std::string_view, 40> osk_keys{
    "A", "B", "C", "D", "E", "F", "G", "H", "I", "J",
    "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T",
    "U", "V", "W", "X", "Y", "Z", "0", "1", "2", "3",
    "4", "5", "6", "7", "8", "9", "Space", "Back", "Next", "Done"};

QApplication* ensure_application() {
  if (qApp != nullptr) return qApp;
#if defined(_WIN32)
  qputenv("QT_QPA_PLATFORM", QByteArray("windows"));
#elif defined(__APPLE__)
  qputenv("QT_QPA_PLATFORM", QByteArray("cocoa"));
#endif
  const auto deployed = QLibraryInfo::path(QLibraryInfo::PluginsPath) +
                        "/platforms";
  const auto plugins = QDir(deployed).exists()
      ? deployed
      : QStringLiteral(REFRACT_QT_PLATFORM_PLUGIN_PATH);
  qputenv("QT_QPA_PLATFORM_PLUGIN_PATH", plugins.toUtf8());
  static int argument_count = 1;
  static char program_name[] = "psprism";
  static char* arguments[]{program_name, nullptr};
  static auto application =
      std::make_unique<QApplication>(argument_count, arguments);
  application->setQuitOnLastWindowClosed(false);
  return application.get();
}

QString text(std::string_view value) {
  return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

QString text(const std::u16string& value) {
  return QString::fromUtf16(value.data(), static_cast<qsizetype>(value.size()));
}

bool is_savedata(host::DialogKind kind) {
  return kind == host::DialogKind::savedata_load ||
         kind == host::DialogKind::savedata_save ||
         kind == host::DialogKind::savedata_delete;
}

class SaveCarousel final : public QWidget {
 public:
  std::function<void(std::size_t)> on_selection_changed;
  std::function<void(std::size_t)> on_item_activated;

  explicit SaveCarousel(QWidget* parent = nullptr) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    animation_.setDuration(180);
    animation_.setEasingCurve(QEasingCurve::OutCubic);
    QObject::connect(&animation_, &QVariantAnimation::valueChanged, this,
                     [this](const QVariant& value) {
                       slide_offset_ = value.toReal();
                       update();
                     });
  }

  void setItems(const std::vector<host::DialogItem>* items,
                std::size_t selected) {
    items_ = items;
    selected_ = selected;
    slide_offset_ = 0.0;
    animation_.stop();
    update();
  }

  void setSelected(std::size_t selected) {
    if (selected == selected_) return;
    const auto direction = selected > selected_ ? 1.0 : -1.0;
    selected_ = selected;
    animation_.stop();
    animation_.setStartValue(direction);
    animation_.setEndValue(0.0);
    animation_.start();
  }

  std::size_t selected() const { return selected_; }

 protected:
  void mousePressEvent(QMouseEvent* event) override {
    if (items_ == nullptr || items_->empty()) return;
    constexpr int card_w = 320;
    constexpr int card_h = 180;
    constexpr int stride = 350;
    constexpr int text_gap = 14;
    constexpr int text_h = 44;
    const int total_content_h = card_h + text_gap + text_h;
    const int center_x = (width() - card_w) / 2;
    const int center_y = (height() - total_content_h) / 2;

    for (long index = static_cast<long>(selected_) - 2;
         index <= static_cast<long>(selected_) + 2; ++index) {
      if (index < 0 || index >= static_cast<long>(items_->size())) continue;
      const int relative = static_cast<int>(index) - static_cast<int>(selected_);
      const qreal pos = static_cast<qreal>(relative) + slide_offset_;
      const qreal dist = std::abs(pos);
      const qreal scale = std::clamp(1.0 - dist * 0.12, 0.75, 1.0);
      const int w = static_cast<int>(card_w * scale);
      const int h = static_cast<int>(card_h * scale);
      const int x = center_x + static_cast<int>(pos * stride) + (card_w - w) / 2;
      const int y = center_y + (card_h - h) / 2;
      const QRect bounds(x, y, w, h);
      if (bounds.contains(event->pos())) {
        if (static_cast<std::size_t>(index) != selected_) {
          setSelected(static_cast<std::size_t>(index));
          if (on_selection_changed) on_selection_changed(selected_);
        } else {
          if (on_item_activated) on_item_activated(selected_);
        }
        break;
      }
    }
  }

  void paintEvent(QPaintEvent*) override {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.fillRect(rect(), Qt::transparent);
    if (items_ == nullptr || items_->empty()) return;

    constexpr int card_w = 320;
    constexpr int card_h = 180;
    constexpr int stride = 350;
    constexpr int text_gap = 14;
    constexpr int text_h = 44;
    const int total_content_h = card_h + text_gap + text_h;

    const int center_x = (width() - card_w) / 2;
    const int center_y = (height() - total_content_h) / 2;

    for (long pass = 0; pass < 2; ++pass) {
      for (long index = static_cast<long>(selected_) - 2;
           index <= static_cast<long>(selected_) + 2; ++index) {
        if (index < 0 || index >= static_cast<long>(items_->size())) continue;
        const int relative = static_cast<int>(index) - static_cast<int>(selected_);
        const bool is_focused = (relative == 0 && std::abs(slide_offset_) < 0.5);
        if ((pass == 0 && is_focused) || (pass == 1 && !is_focused)) continue;

        const qreal pos = static_cast<qreal>(relative) + slide_offset_;
        const qreal dist = std::abs(pos);
        const qreal scale = std::clamp(1.0 - dist * 0.12, 0.75, 1.0);
        const qreal opacity = std::clamp(1.0 - dist * 0.65, 0.25, 1.0);

        const int w = static_cast<int>(card_w * scale);
        const int h = static_cast<int>(card_h * scale);
        const int x = center_x + static_cast<int>(pos * stride) + (card_w - w) / 2;
        const int y = center_y + (card_h - h) / 2;
        const QRect bounds(x, y, w, h);

        painter.save();
        painter.setOpacity(opacity);

        if (dist < 0.5) {
          painter.save();
          painter.setPen(Qt::NoPen);
          painter.setBrush(QColor(0, 120, 212, static_cast<int>(40 * (1.0 - dist * 2.0))));
          painter.drawRoundedRect(bounds.adjusted(-5, -5, 5, 5), 12, 12);
          painter.restore();
        }

        painter.setBrush(QColor(14, 18, 23));
        if (dist < 0.5) {
          painter.setPen(QPen(QColor(0, 140, 240), 2.0));
        } else {
          painter.setPen(QPen(QColor(38, 46, 56), 1.5));
        }
        painter.drawRoundedRect(bounds, 8, 8);

        const auto inner = bounds.adjusted(2, 2, -2, -2);
        QPainterPath clip_path;
        clip_path.addRoundedRect(inner, 6, 6);
        painter.setClipPath(clip_path);

        const auto& item = (*items_)[index];
        if (item.empty) {
          painter.fillRect(inner, QColor(16, 20, 26));
          painter.setPen(dist < 0.5 ? QColor(0, 140, 240) : QColor(100, 116, 139));
          QFont empty_font = painter.font();
          empty_font.setPixelSize(14);
          empty_font.setWeight(QFont::DemiBold);
          painter.setFont(empty_font);
          painter.drawText(inner, Qt::AlignCenter, tr("Empty Slot"));
        } else {
          const auto preview_img = item.preview_png.empty() ? QImage{} : QImage::fromData(
              item.preview_png.data(), static_cast<int>(item.preview_png.size()), "PNG");
          const auto icon_img = item.icon_png.empty() ? QImage{} : QImage::fromData(
              item.icon_png.data(), static_cast<int>(item.icon_png.size()), "PNG");

          if (!preview_img.isNull()) {
            const auto scaled_preview = preview_img.scaled(
                inner.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            painter.drawImage(inner.topLeft(), scaled_preview);

            if (!icon_img.isNull()) {
              const int icon_w = static_cast<int>(68 * scale);
              const int icon_h = static_cast<int>(38 * scale);
              const QRect icon_badge(inner.left() + 8, inner.bottom() - icon_h - 8, icon_w, icon_h);
              painter.fillRect(icon_badge.adjusted(-2, -2, 2, 2), QColor(0, 0, 0, 170));
              painter.setPen(QPen(QColor(255, 255, 255, 70), 1));
              painter.drawRoundedRect(icon_badge.adjusted(-2, -2, 2, 2), 4, 4);
              const auto scaled_icon = icon_img.scaled(
                  icon_badge.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
              const int ix = icon_badge.center().x() - scaled_icon.width() / 2;
              const int iy = icon_badge.center().y() - scaled_icon.height() / 2;
              painter.drawImage(ix, iy, scaled_icon);
            }
          } else if (!icon_img.isNull()) {
            const auto scaled_icon = icon_img.scaled(
                inner.size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
            const int ix = inner.center().x() - scaled_icon.width() / 2;
            const int iy = inner.center().y() - scaled_icon.height() / 2;
            painter.drawImage(ix, iy, scaled_icon);
          } else {
            painter.fillRect(inner, QColor(22, 26, 33));
            painter.setPen(QColor(148, 163, 184));
            painter.drawText(inner, Qt::AlignCenter, tr("No Preview"));
          }
        }
        painter.restore();
      }
    }

    if (selected_ < items_->size()) {
      const auto& item = (*items_)[selected_];
      const int text_y = center_y + card_h + text_gap;
      const int text_w = card_w + 200;
      const int text_x = center_x - 100;

      painter.save();
      const qreal text_opacity = std::clamp(1.0 - std::abs(slide_offset_) * 1.5, 0.0, 1.0);
      painter.setOpacity(text_opacity);

      QString title_str;
      QString meta_str;

      if (item.empty) {
        title_str = QStringLiteral("New Save Data");
        meta_str = item.detail.empty() ? QStringLiteral("Empty Slot") : text(item.detail);
      } else {
        title_str = text(item.title);
        QStringList meta_parts;
        if (!item.subtitle.empty()) meta_parts << text(item.subtitle);
        if (!item.timestamp.empty()) meta_parts << text(item.timestamp);
        if (!item.size.empty()) meta_parts << text(item.size);
        if (meta_parts.isEmpty() && !item.detail.empty()) meta_parts << text(item.detail);
        meta_str = meta_parts.join(QStringLiteral("   ·   "));
      }

      QFont title_font = painter.font();
      title_font.setPixelSize(15);
      title_font.setWeight(QFont::Bold);
      painter.setFont(title_font);
      painter.setPen(QColor(255, 255, 255));
      painter.drawText(QRect(text_x, text_y, text_w, 22), Qt::AlignCenter, title_str);

      QFont meta_font = painter.font();
      meta_font.setPixelSize(12);
      meta_font.setWeight(QFont::Normal);
      painter.setFont(meta_font);
      painter.setPen(QColor(56, 189, 248));
      painter.drawText(QRect(text_x, text_y + 22, text_w, 20), Qt::AlignCenter, meta_str);

      painter.restore();
    }
  }

 private:
  const std::vector<host::DialogItem>* items_{};
  std::size_t selected_{};
  qreal slide_offset_{};
  QVariantAnimation animation_;
};

} // namespace

struct DialogFrontend::Implementation {
  std::recursive_mutex mutex;
  std::optional<host::DialogModel> model;
  std::optional<host::DialogResult> result;
  QPointer<QDialog> dialog;
  SaveCarousel* carousel{};
  QPushButton* yes_button_{};
  QPushButton* no_button_{};
  std::vector<QLineEdit*> editors;
  std::vector<QPushButton*> osk_buttons;
  QPointer<QWidget> mouse_target;
  std::size_t selected_item{};
  std::size_t selected_field{};
  std::size_t selected_key{};
  bool affirmative{true};
  bool finishing{};

  void send_mouse_event(QEvent::Type type, double x, double y) {
    auto* root = dialog.data();
    if (!model || root == nullptr) return;
    const QPoint root_position{static_cast<int>(std::floor(x)),
                               static_cast<int>(std::floor(y))};
    QWidget* target = mouse_target.data();
    if (type == QEvent::MouseButtonPress || target == nullptr) {
      target = root->childAt(root_position);
      if (target == nullptr) target = root;
    }
    if (type == QEvent::MouseButtonPress) mouse_target = target;
    const QPointF local_position{target->mapFrom(root, root_position)};
    const QPointF global_position{root->mapToGlobal(root_position)};
    const auto button = type == QEvent::MouseMove ? Qt::NoButton
                                                   : Qt::LeftButton;
    const auto buttons = type == QEvent::MouseButtonPress ||
                                 (type == QEvent::MouseMove && mouse_target)
                             ? Qt::LeftButton
                             : Qt::NoButton;
    QMouseEvent event(type, local_position, global_position, button, buttons,
                      Qt::NoModifier);
    QCoreApplication::sendEvent(target, &event);
    if (type == QEvent::MouseButtonRelease) mouse_target.clear();
  }

  void complete(bool cancelled) {
    if (!model || finishing) return;
    finishing = true;
    host::DialogResult completed;
    completed.id = model->id;
    completed.cancelled = cancelled;
    completed.affirmative = affirmative;
    completed.selected_item = selected_item;
    if (!editors.empty()) {
      for (auto* editor : editors) {
        const auto value = editor->text();
        completed.field_text.emplace_back(
            reinterpret_cast<const char16_t*>(value.utf16()), value.size());
      }
    } else {
      for (const auto& field : model->fields)
        completed.field_text.push_back(field.text);
    }
    result = std::move(completed);
    model.reset();
    if (dialog) dialog->close();
  }

  void update_savedata_labels() {
    if (!model || model->items.empty() || carousel == nullptr) return;
    carousel->setSelected(selected_item);
  }

  void update_yes_no_highlight() {
    if (yes_button_ == nullptr || no_button_ == nullptr) return;
    if (affirmative) {
      yes_button_->setStyleSheet(
          "QPushButton { background: #0078d4; color: #ffffff; font-size: 14px; font-weight: 600; "
          "border: 1.5px solid #38bdf8; border-radius: 8px; padding: 8px 24px; } "
          "QPushButton:hover { background: #1084e3; }");
      no_button_->setStyleSheet(
          "QPushButton { background: #161b22; color: #94a3b8; font-size: 14px; font-weight: 500; "
          "border: 1px solid #283340; border-radius: 8px; padding: 8px 24px; } "
          "QPushButton:hover { background: #202732; color: #ffffff; }");
    } else {
      yes_button_->setStyleSheet(
          "QPushButton { background: #161b22; color: #94a3b8; font-size: 14px; font-weight: 500; "
          "border: 1px solid #283340; border-radius: 8px; padding: 8px 24px; } "
          "QPushButton:hover { background: #202732; color: #ffffff; }");
      no_button_->setStyleSheet(
          "QPushButton { background: #0078d4; color: #ffffff; font-size: 14px; font-weight: 600; "
          "border: 1.5px solid #38bdf8; border-radius: 8px; padding: 8px 24px; } "
          "QPushButton:hover { background: #1084e3; }");
    }
  }

  void move(int direction) {
    if (!model) return;
    if (!model->items.empty()) {
      const auto next = static_cast<long>(selected_item) + direction;
      if (next < 0 || next >= static_cast<long>(model->items.size())) return;
      selected_item = static_cast<std::size_t>(next);
      update_savedata_labels();
    } else if (model->yes_no) {
      affirmative = direction < 0;
      update_yes_no_highlight();
    }
  }

  void set_osK_highlight() {
    for (std::size_t index = 0; index < osk_buttons.size(); ++index) {
      if (index == selected_key) {
        osk_buttons[index]->setStyleSheet(
            "QPushButton { background: #0078d4; color: #ffffff; font-weight: 700; "
            "border: 1.5px solid #38bdf8; border-radius: 6px; } "
            "QPushButton:hover { background: #1084e3; }");
      } else {
        osk_buttons[index]->setStyleSheet(
            "QPushButton { background: #161b22; color: #e2e8f0; border: 1px solid #283340; "
            "border-radius: 6px; font-weight: 500; } "
            "QPushButton:hover { background: #202732; border: 1px solid #38bdf8; }");
      }
    }
  }

  void backspace() {
    if (!editors.empty()) {
      auto* editor = editors[selected_field];
      auto value = editor->text();
      value.chop(1);
      editor->setText(value);
    } else if (model && !model->fields.empty()) {
      auto& value = model->fields[selected_field].text;
      if (!value.empty()) value.pop_back();
    }
  }

  void activate_osk_key() {
    if (!model) return;
    if (selected_key < 36U && !editors.empty())
      editors[selected_field]->insert(text(osk_keys[selected_key]));
    else if (selected_key == 36U && !editors.empty()) editors[selected_field]->insert(" ");
    else if (selected_key == 37U) backspace();
    else if (selected_key == 38U && !editors.empty()) {
      selected_field = (selected_field + 1U) % editors.size();
      editors[selected_field]->setFocus();
    } else if (selected_key == 39U) complete(false);
  }

  void build_savedata() {
    auto* outer_layout = new QVBoxLayout(dialog);
    outer_layout->setContentsMargins(28, 20, 28, 18);
    outer_layout->setSpacing(0);

    QHBoxLayout* header_bar = new QHBoxLayout;
    QLabel* header_title = new QLabel(dialog);
    QString kind_str = model->kind == host::DialogKind::savedata_load
                           ? QStringLiteral("LOAD")
                       : model->kind == host::DialogKind::savedata_delete
                           ? QStringLiteral("DELETE")
                           : QStringLiteral("SAVE");
    header_title->setText(QStringLiteral("💾  ") + text(model->title).toUpper() +
                          QStringLiteral("  —  ") + kind_str);
    header_title->setStyleSheet(
        "color: #94a3b8; font-size: 13px; font-weight: 700; letter-spacing: 1.5px;");
    header_bar->addWidget(header_title);
    header_bar->addStretch(1);
    outer_layout->addLayout(header_bar);

    carousel = new SaveCarousel(dialog);
    carousel->setItems(&model->items, selected_item);
    carousel->on_selection_changed = [this](std::size_t index) {
      selected_item = index;
      carousel->setSelected(selected_item);
    };
    carousel->on_item_activated = [this](std::size_t) {
      complete(false);
    };
    outer_layout->addWidget(carousel, 1);

    QHBoxLayout* bottom_bar = new QHBoxLayout;
    bottom_bar->setContentsMargins(0, 8, 0, 0);

    QLabel* legend = new QLabel(dialog);
    legend->setText(model->confirm_with_cross
                        ? QStringLiteral("◄ ►  Select Slot     ✕  ") +
                              text(model->accept_label) +
                              QStringLiteral("     ○  ") +
                              text(model->cancel_label)
                        : QStringLiteral("◄ ►  Select Slot     ○  ") +
                              text(model->accept_label) +
                              QStringLiteral("     ✕  ") +
                              text(model->cancel_label));
    legend->setStyleSheet("color: #64748b; font-size: 12px; font-weight: 500;");

    QPushButton* cancel_btn = new QPushButton(text(model->cancel_label), dialog);
    cancel_btn->setStyleSheet(
        "QPushButton { background: #161b22; color: #cbd5e1; font-size: 13px; font-weight: 500; "
        "padding: 7px 18px; border: 1px solid #283340; border-radius: 6px; } "
        "QPushButton:hover { background: #202732; color: #ffffff; }");

    QPushButton* accept_btn = new QPushButton(text(model->accept_label), dialog);
    accept_btn->setStyleSheet(
        "QPushButton { background: #0078d4; color: #ffffff; font-size: 13px; font-weight: 600; "
        "padding: 7px 22px; border-radius: 6px; border: 0; } "
        "QPushButton:hover { background: #1084e3; }");

    QObject::connect(accept_btn, &QPushButton::clicked, dialog, [this] { complete(false); });
    QObject::connect(cancel_btn, &QPushButton::clicked, dialog, [this] { complete(true); });

    bottom_bar->addWidget(legend);
    bottom_bar->addStretch(1);
    bottom_bar->addWidget(cancel_btn);
    bottom_bar->addWidget(accept_btn);

    outer_layout->addLayout(bottom_bar);
  }

  void build_generic() {
    if (model->kind == host::DialogKind::osk) {
      build_osk();
      return;
    }

    auto* outer_layout = new QVBoxLayout(dialog);
    outer_layout->setContentsMargins(20, 20, 20, 20);
    outer_layout->addStretch(1);

    QWidget* modal_card = new QWidget(dialog);
    modal_card->setObjectName("modalCard");
    modal_card->setStyleSheet(
        "#modalCard { background: #0c1015; border: 1px solid #1f2937; "
        "border-radius: 12px; }");
    modal_card->setMinimumWidth(480);
    modal_card->setMaximumWidth(560);

    QVBoxLayout* card_layout = new QVBoxLayout(modal_card);
    card_layout->setContentsMargins(32, 26, 32, 26);
    card_layout->setSpacing(14);

    const QString raw_msg = text(model->message);
    const bool is_error = raw_msg.startsWith("Error", Qt::CaseInsensitive) ||
                          raw_msg.startsWith("0x8", Qt::CaseInsensitive) ||
                          raw_msg.startsWith("80", Qt::CaseInsensitive);

    if (is_error) {
      QLabel* error_badge = new QLabel(modal_card);
      error_badge->setText(QStringLiteral("⚠  SYSTEM ERROR"));
      error_badge->setAlignment(Qt::AlignCenter);
      error_badge->setStyleSheet(
          "background: #3f1515; color: #fca5a5; font-size: 11px; font-weight: 700; "
          "padding: 4px 14px; border-radius: 10px; border: 1px solid #7f1d1d;");
      error_badge->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
      card_layout->addWidget(error_badge, 0, Qt::AlignCenter);
    }

    QLabel* title_label = new QLabel(text(model->title), modal_card);
    title_label->setAlignment(Qt::AlignCenter);
    title_label->setStyleSheet("font-size: 17px; font-weight: 700; color: #ffffff;");
    card_layout->addWidget(title_label);

    QFrame* divider = new QFrame(modal_card);
    divider->setFrameShape(QFrame::HLine);
    divider->setStyleSheet("color: #1e2631; background: #1e2631; max-height: 1px;");
    card_layout->addWidget(divider);

    QLabel* message_label = new QLabel(modal_card);
    message_label->setAlignment(Qt::AlignCenter);
    message_label->setWordWrap(true);
    message_label->setStyleSheet("font-size: 15px; color: #f1f5f9; line-height: 1.45;");
    message_label->setText(raw_msg);
    card_layout->addWidget(message_label);

    if (!model->detail.empty()) {
      QLabel* detail_label = new QLabel(text(model->detail), modal_card);
      detail_label->setAlignment(Qt::AlignCenter);
      detail_label->setWordWrap(true);
      detail_label->setStyleSheet("font-size: 13px; color: #94a3b8;");
      card_layout->addWidget(detail_label);
    }

    card_layout->addSpacing(6);

    if (model->yes_no) {
      QHBoxLayout* btn_row = new QHBoxLayout;
      btn_row->setSpacing(16);
      btn_row->addStretch(1);

      yes_button_ = new QPushButton(text(model->yes_label), modal_card);
      yes_button_->setMinimumWidth(120);
      yes_button_->setMinimumHeight(38);

      no_button_ = new QPushButton(text(model->no_label), modal_card);
      no_button_->setMinimumWidth(120);
      no_button_->setMinimumHeight(38);

      QObject::connect(yes_button_, &QPushButton::clicked, dialog, [this] {
        affirmative = true;
        complete(false);
      });
      QObject::connect(no_button_, &QPushButton::clicked, dialog, [this] {
        affirmative = false;
        complete(false);
      });

      btn_row->addWidget(yes_button_);
      btn_row->addWidget(no_button_);
      btn_row->addStretch(1);
      card_layout->addLayout(btn_row);
      update_yes_no_highlight();
    } else {
      QHBoxLayout* btn_row = new QHBoxLayout;
      btn_row->addStretch(1);
      QPushButton* ok_button = new QPushButton(text(model->accept_label), modal_card);
      ok_button->setMinimumWidth(130);
      ok_button->setMinimumHeight(38);
      ok_button->setStyleSheet(
          "QPushButton { background: #0078d4; color: #ffffff; font-size: 14px; "
          "font-weight: 600; border-radius: 8px; padding: 8px 22px; border: 0; } "
          "QPushButton:hover { background: #1084e3; }");
      QObject::connect(ok_button, &QPushButton::clicked, dialog, [this] {
        complete(false);
      });
      btn_row->addWidget(ok_button);
      btn_row->addStretch(1);
      card_layout->addLayout(btn_row);
    }

    outer_layout->addWidget(modal_card, 0, Qt::AlignCenter);
    outer_layout->addStretch(1);

    QHBoxLayout* bottom_bar = new QHBoxLayout;
    QLabel* legend = new QLabel(dialog);
    if (model->yes_no) {
      legend->setText(model->confirm_with_cross
                          ? QStringLiteral("◄ ►  Select     ✕  Confirm     ○  Cancel")
                          : QStringLiteral("◄ ►  Select     ○  Confirm     ✕  Cancel"));
    } else {
      legend->setText(model->confirm_with_cross
                          ? QStringLiteral("✕  ") + text(model->accept_label)
                          : QStringLiteral("○  ") + text(model->accept_label));
    }
    legend->setAlignment(Qt::AlignCenter);
    legend->setStyleSheet("color: #64748b; font-size: 12px;");
    bottom_bar->addWidget(legend);
    outer_layout->addLayout(bottom_bar);
  }

  void build_osk() {
    auto* outer_layout = new QVBoxLayout(dialog);
    outer_layout->setContentsMargins(20, 20, 20, 20);
    outer_layout->addStretch(1);

    QWidget* modal_card = new QWidget(dialog);
    modal_card->setObjectName("oskCard");
    modal_card->setStyleSheet(
        "#oskCard { background: #0c1015; border: 1px solid #1f2937; "
        "border-radius: 12px; }");
    modal_card->setMinimumWidth(620);
    modal_card->setMaximumWidth(700);

    QVBoxLayout* card_layout = new QVBoxLayout(modal_card);
    card_layout->setContentsMargins(28, 22, 28, 22);
    card_layout->setSpacing(14);

    QLabel* header_title = new QLabel(text(model->title).toUpper(), modal_card);
    header_title->setAlignment(Qt::AlignCenter);
    header_title->setStyleSheet("color: #94a3b8; font-size: 14px; font-weight: 700; letter-spacing: 1.5px;");
    card_layout->addWidget(header_title);

    for (std::size_t i = 0; i < model->fields.size(); ++i) {
      const auto& field = model->fields[i];
      QVBoxLayout* field_box = new QVBoxLayout;
      field_box->setSpacing(4);

      QHBoxLayout* label_row = new QHBoxLayout;
      QLabel* field_label = new QLabel(text(field.label), modal_card);
      field_label->setStyleSheet("color: #cbd5e1; font-size: 13px; font-weight: 600;");
      QLabel* count_label = new QLabel(modal_card);
      count_label->setStyleSheet("color: #64748b; font-size: 11px; font-weight: 500;");
      const auto current_len = text(field.text).length();
      count_label->setText(field.limit != 0U
                               ? QStringLiteral("%1 / %2").arg(current_len).arg(field.limit)
                               : QStringLiteral("%1").arg(current_len));
      label_row->addWidget(field_label);
      label_row->addStretch(1);
      label_row->addWidget(count_label);
      field_box->addLayout(label_row);

      QLineEdit* editor = new QLineEdit(text(field.text), modal_card);
      if (field.limit != 0U) editor->setMaxLength(static_cast<int>(field.limit));
      editor->setStyleSheet(
          "QLineEdit { background: #070a0e; color: #ffffff; border: 1.5px solid #0078d4; "
          "border-radius: 6px; padding: 8px 12px; font-size: 15px; } "
          "QLineEdit:focus { border: 1.5px solid #38bdf8; }");
      QObject::connect(editor, &QLineEdit::textChanged, modal_card,
                       [count_label, limit = field.limit](const QString& txt) {
                         count_label->setText(limit != 0U
                                                  ? QStringLiteral("%1 / %2").arg(txt.length()).arg(limit)
                                                  : QStringLiteral("%1").arg(txt.length()));
                       });
      field_box->addWidget(editor);
      editors.push_back(editor);
      card_layout->addLayout(field_box);
    }

    QGridLayout* grid = new QGridLayout;
    grid->setSpacing(5);
    for (std::size_t index = 0; index < osk_keys.size(); ++index) {
      QString label_txt;
      if (index == 36U) label_txt = QStringLiteral("␣ Space");
      else if (index == 37U) label_txt = QStringLiteral("⌫ Del");
      else if (index == 38U) label_txt = QStringLiteral("⇥ Next");
      else if (index == 39U) label_txt = QStringLiteral("✓ Done");
      else label_txt = text(osk_keys[index]);

      auto* button = new QPushButton(label_txt, modal_card);
      button->setMinimumWidth(52);
      button->setMinimumHeight(38);
      button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
      QObject::connect(button, &QPushButton::clicked, dialog, [this, index] {
        selected_key = index;
        activate_osk_key();
        set_osK_highlight();
      });
      grid->addWidget(button, static_cast<int>(index / 10U),
                      static_cast<int>(index % 10U));
      osk_buttons.push_back(button);
    }
    card_layout->addLayout(grid);
    set_osK_highlight();
    if (!editors.empty()) editors.front()->setFocus();

    outer_layout->addWidget(modal_card, 0, Qt::AlignCenter);
    outer_layout->addStretch(1);

    QHBoxLayout* bottom_bar = new QHBoxLayout;
    QLabel* legend = new QLabel(dialog);
    legend->setText(model->confirm_with_cross
                        ? QStringLiteral("✕  Input     □  Backspace     START  Done     ○  Cancel")
                        : QStringLiteral("○  Input     □  Backspace     START  Done     ✕  Cancel"));
    legend->setAlignment(Qt::AlignCenter);
    legend->setStyleSheet("color: #64748b; font-size: 12px; font-weight: 500;");
    bottom_bar->addWidget(legend);
    outer_layout->addLayout(bottom_bar);
  }
};

DialogFrontend::DialogFrontend()
    : implementation_(std::make_unique<Implementation>()) {
  static_cast<void>(ensure_application());
}

DialogFrontend::~DialogFrontend() = default;

void DialogFrontend::present(host::DialogModel model) {
  std::lock_guard lock(implementation_->mutex);
  implementation_->complete(true);
  implementation_->model = std::move(model);
  implementation_->selected_item = std::min(implementation_->model->selected_item,
      implementation_->model->items.empty() ? 0U
                                             : implementation_->model->items.size() - 1U);
  implementation_->selected_field = 0;
  implementation_->selected_key = 0;
  implementation_->affirmative = !implementation_->model->default_no;
  implementation_->finishing = false;
  auto* dialog = new QDialog;
  implementation_->dialog = dialog;
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setModal(false);
  dialog->setWindowTitle(text(implementation_->model->title));
  dialog->setMinimumSize(is_savedata(implementation_->model->kind)
                             ? QSize(800, 480)
                             : (implementation_->model->kind == host::DialogKind::osk
                                    ? QSize(720, 500)
                                    : QSize(560, 400)));
  dialog->resize(is_savedata(implementation_->model->kind)
                     ? QSize(960, 544)
                     : (implementation_->model->kind == host::DialogKind::osk
                            ? QSize(800, 540)
                            : QSize(640, 440)));
  dialog->setStyleSheet(
      "QDialog { background: #000000; color: #f8fafc; font-family: 'Helvetica Neue'; }"
      "QLabel { color: #f8fafc; font-family: 'Helvetica Neue'; }"
      "QPushButton { font-family: 'Helvetica Neue'; }"
      "QLineEdit { font-family: 'Helvetica Neue'; }");
  if (is_savedata(implementation_->model->kind)) implementation_->build_savedata();
  else implementation_->build_generic();
}

void DialogFrontend::dismiss(std::uint64_t id) {
  std::lock_guard lock(implementation_->mutex);
  if (implementation_->model && implementation_->model->id == id) {
    implementation_->finishing = true;
    implementation_->model.reset();
    if (implementation_->dialog) implementation_->dialog->close();
  }
}

bool DialogFrontend::visible() const {
  std::lock_guard lock(implementation_->mutex);
  return implementation_->model.has_value();
}

std::uint64_t DialogFrontend::id() const {
  std::lock_guard lock(implementation_->mutex);
  return implementation_->model ? implementation_->model->id : 0U;
}

void DialogFrontend::handle_buttons(std::uint32_t buttons) {
  std::lock_guard lock(implementation_->mutex);
  auto& state = *implementation_;
  if (!state.model) return;
  const auto confirm = state.model->confirm_with_cross ? psp_cross : psp_circle;
  const auto cancel = state.model->confirm_with_cross ? psp_circle : psp_cross;
  if (state.model->kind == host::DialogKind::osk) {
    if ((buttons & psp_left) != 0U && state.selected_key % 10U != 0U)
      --state.selected_key;
    if ((buttons & psp_right) != 0U && state.selected_key % 10U != 9U)
      ++state.selected_key;
    if ((buttons & psp_up) != 0U && state.selected_key >= 10U)
      state.selected_key -= 10U;
    if ((buttons & psp_down) != 0U && state.selected_key + 10U < osk_keys.size())
      state.selected_key += 10U;
    if ((buttons & psp_square) != 0U) state.backspace();
    if ((buttons & confirm) != 0U) state.activate_osk_key();
    if ((buttons & psp_start) != 0U) state.complete(false);
    else if ((buttons & cancel) != 0U) state.complete(true);
    state.set_osK_highlight();
    return;
  }
  if ((buttons & psp_left) != 0U) state.move(-1);
  if ((buttons & psp_right) != 0U) state.move(1);
  if ((buttons & confirm) != 0U) state.complete(false);
  else if ((buttons & cancel) != 0U) state.complete(true);
}

void DialogFrontend::accept() {
  std::lock_guard lock(implementation_->mutex);
  implementation_->complete(false);
}

void DialogFrontend::cancel() {
  std::lock_guard lock(implementation_->mutex);
  implementation_->complete(true);
}

void DialogFrontend::handle_text(std::u16string_view value) {
  std::lock_guard lock(implementation_->mutex);
  if (!implementation_->model || implementation_->editors.empty()) return;
  auto* editor = implementation_->editors[implementation_->selected_field];
  editor->insert(QString::fromUtf16(value.data(), static_cast<qsizetype>(value.size())));
}

void DialogFrontend::handle_backspace() {
  std::lock_guard lock(implementation_->mutex);
  implementation_->backspace();
}

void DialogFrontend::handle_mouse_move(double x, double y) {
  std::lock_guard lock(implementation_->mutex);
  implementation_->send_mouse_event(QEvent::MouseMove, x, y);
}

void DialogFrontend::handle_mouse_press(double x, double y) {
  std::lock_guard lock(implementation_->mutex);
  implementation_->send_mouse_event(QEvent::MouseButtonPress, x, y);
}

void DialogFrontend::handle_mouse_release(double x, double y) {
  std::lock_guard lock(implementation_->mutex);
  implementation_->send_mouse_event(QEvent::MouseButtonRelease, x, y);
}

std::optional<host::DialogResult> DialogFrontend::take_result(std::uint64_t id) {
  std::lock_guard lock(implementation_->mutex);
  if (!implementation_->result || implementation_->result->id != id)
    return std::nullopt;
  auto result = std::move(implementation_->result);
  implementation_->result.reset();
  return result;
}

DialogFrame DialogFrontend::rendered_frame(double device_pixel_ratio,
                                           std::uint32_t logical_width,
                                           std::uint32_t logical_height) const {
  std::lock_guard lock(implementation_->mutex);
  auto* dialog = implementation_->dialog.data();
  if (!implementation_->model || dialog == nullptr) return {};
  if (logical_width != 0U && logical_height != 0U) {
    dialog->resize(static_cast<int>(logical_width),
                   static_cast<int>(logical_height));
    if (dialog->layout() != nullptr) dialog->layout()->activate();
  }
  const auto scale = std::max(1.0, device_pixel_ratio);
  const QSize pixel_size{
      static_cast<int>(std::ceil(dialog->width() * scale)),
      static_cast<int>(std::ceil(dialog->height() * scale))};
  QImage image(pixel_size, QImage::Format_RGBA8888_Premultiplied);
  image.setDevicePixelRatio(scale);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  dialog->render(&painter);
  DialogFrame frame;
  frame.width = static_cast<std::uint32_t>(image.width());
  frame.height = static_cast<std::uint32_t>(image.height());
  frame.pixels.assign(image.constBits(),
                      image.constBits() + image.sizeInBytes());
  return frame;
}

int run_desktop_dialog_event_loop() {
  return ensure_application()->exec();
}

void request_desktop_dialog_event_loop_exit() {
  if (qApp != nullptr) qApp->quit();
}

void process_desktop_dialog_events() {
  if (qApp != nullptr) QCoreApplication::processEvents();
}

} // namespace refract::desktop
