#include "desktop_dialogs.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFont>
#include <QFormLayout>
#include <QGridLayout>
#include <QGuiApplication>
#include <QImage>
#include <QLabel>
#include <QLibraryInfo>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
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
    "4", "5", "6", "7", "8", "9", "Space", "Back", "Field", "Done"};

QApplication* ensure_application() {
  if (qApp != nullptr) return qApp;
  qputenv("QT_QPA_PLATFORM", QByteArray("cocoa"));
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

QImage slot_image(const host::DialogItem& item) {
  const auto& bytes = !item.preview_png.empty() ? item.preview_png
                                                : item.icon_png;
  return bytes.empty() ? QImage{} : QImage::fromData(
      bytes.data(), static_cast<int>(bytes.size()), "PNG");
}

class SaveCarousel final : public QWidget {
 public:
  explicit SaveCarousel(QWidget* parent = nullptr) : QWidget(parent) {
    setMinimumHeight(230);
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

 protected:
  void paintEvent(QPaintEvent*) override {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor(18, 21, 24));
    if (items_ == nullptr || items_->empty()) return;
    constexpr int width = 290;
    constexpr int height = 164;
    constexpr int stride = 330;
    const int center = (this->width() - width) / 2;
    for (long index = static_cast<long>(selected_) - 2;
         index <= static_cast<long>(selected_) + 2; ++index) {
      if (index < 0 || index >= static_cast<long>(items_->size())) continue;
      const int relative = static_cast<int>(index) - static_cast<int>(selected_);
      const QRect bounds(center + static_cast<int>((relative + slide_offset_) * stride),
                         (this->height() - height) / 2, width, height);
      const qreal opacity = relative == 0 ? 1.0 : 0.34;
      painter.save();
      painter.setOpacity(opacity);
      painter.setBrush(QColor(28, 32, 36));
      painter.setPen(QPen(relative == 0 ? QColor(210, 216, 221)
                                        : QColor(90, 98, 105), 2));
      painter.drawRoundedRect(bounds, 6, 6);
      const auto image = slot_image((*items_)[index]);
      const auto inner = bounds.adjusted(6, 6, -6, -6);
      if (image.isNull()) {
        painter.fillRect(inner, QColor(43, 47, 51));
        painter.setPen(QColor(190, 195, 198));
        painter.drawText(inner, Qt::AlignCenter,
                         (*items_)[index].empty ? tr("Empty slot")
                                                : tr("No preview"));
      } else {
        const auto scaled = image.scaled(inner.size(), Qt::KeepAspectRatio,
                                         Qt::SmoothTransformation);
        painter.drawImage(inner.center() - QPoint(scaled.width() / 2,
                                                  scaled.height() / 2), scaled);
      }
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
  QLabel* title{};
  QLabel* metadata{};
  QLabel* subtitle{};
  QLabel* detail{};
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
    if (!model || model->items.empty()) return;
    const auto& item = model->items[selected_item];
    title->setText(text(item.title));
    QString info = text(item.timestamp);
    if (!item.size.empty()) info += " · " + text(item.size);
    metadata->setText(info);
    subtitle->setText(text(item.subtitle));
    detail->setText(text(item.detail));
    carousel->setSelected(selected_item);
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
    }
  }

  void set_osK_highlight() {
    for (std::size_t index = 0; index < osk_buttons.size(); ++index) {
      osk_buttons[index]->setStyleSheet(index == selected_key
          ? "QPushButton { background: #2c87cc; color: white; }"
          : "QPushButton { background: #353b42; color: white; }");
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
    auto* layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(12);
    carousel = new SaveCarousel(dialog);
    carousel->setItems(&model->items, selected_item);
    layout->addWidget(carousel);
    title = new QLabel(dialog);
    title->setStyleSheet("font-size: 22px; font-weight: 600;");
    metadata = new QLabel(dialog);
    metadata->setStyleSheet("color: #8c969f;");
    subtitle = new QLabel(dialog);
    subtitle->setStyleSheet("font-size: 18px; font-weight: 600;");
    detail = new QLabel(dialog);
    detail->setWordWrap(true);
    layout->addWidget(title);
    layout->addWidget(metadata);
    layout->addWidget(subtitle);
    layout->addWidget(detail, 1);
    auto* buttons = new QDialogButtonBox(dialog);
    auto* accept = buttons->addButton(text(model->accept_label),
                                      QDialogButtonBox::AcceptRole);
    auto* cancel = buttons->addButton(text(model->cancel_label),
                                      QDialogButtonBox::RejectRole);
    QObject::connect(accept, &QPushButton::clicked, dialog,
                     [this] { complete(false); });
    QObject::connect(cancel, &QPushButton::clicked, dialog,
                     [this] { complete(true); });
    layout->addWidget(buttons);
    update_savedata_labels();
  }

  void build_generic() {
    auto* layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(24, 22, 24, 22);
    layout->setSpacing(14);
    auto* message = new QLabel(text(model->message), dialog);
    message->setWordWrap(true);
    message->setStyleSheet("font-size: 16px;");
    layout->addWidget(message);
    if (model->kind == host::DialogKind::osk) {
      auto* fields = new QFormLayout;
      for (const auto& field : model->fields) {
        auto* editor = new QLineEdit(text(field.text), dialog);
        if (field.limit != 0U) editor->setMaxLength(field.limit);
        fields->addRow(text(field.label), editor);
        editors.push_back(editor);
      }
      layout->addLayout(fields);
      auto* grid = new QGridLayout;
      for (std::size_t index = 0; index < osk_keys.size(); ++index) {
        auto* button = new QPushButton(text(osk_keys[index]), dialog);
        button->setMinimumWidth(58);
        QObject::connect(button, &QPushButton::clicked, dialog, [this, index] {
          selected_key = index;
          activate_osk_key();
          set_osK_highlight();
        });
        grid->addWidget(button, static_cast<int>(index / 10U),
                        static_cast<int>(index % 10U));
        osk_buttons.push_back(button);
      }
      layout->addLayout(grid);
      set_osK_highlight();
      if (!editors.empty()) editors.front()->setFocus();
    }
    auto* buttons = new QDialogButtonBox(dialog);
    auto* accept = buttons->addButton(text(model->accept_label),
                                      QDialogButtonBox::AcceptRole);
    auto* cancel = buttons->addButton(text(model->cancel_label),
                                      QDialogButtonBox::RejectRole);
    QObject::connect(accept, &QPushButton::clicked, dialog,
                     [this] { complete(false); });
    QObject::connect(cancel, &QPushButton::clicked, dialog,
                     [this] { complete(true); });
    layout->addWidget(buttons);
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
  dialog->setMinimumSize(is_savedata(implementation_->model->kind) ? QSize(820, 600)
                                                                     : QSize(620, 420));
  dialog->resize(is_savedata(implementation_->model->kind) ? QSize(960, 680)
                                                           : QSize(700, 500));
  dialog->setStyleSheet(
      "QDialog { background: #191d21; color: #f2f4f5; }"
      "QLabel { color: #f2f4f5; }"
      "QPushButton { background: #353b42; color: #f2f4f5; border: 0; "
      "border-radius: 5px; padding: 8px 14px; }"
      "QPushButton:hover { background: #477da5; }"
      "QLineEdit { background: #242a30; color: #f2f4f5; border: 1px solid "
      "#46515c; border-radius: 4px; padding: 6px; }");
  if (is_savedata(implementation_->model->kind)) implementation_->build_savedata();
  else implementation_->build_generic();
  // The widget tree is rendered into a Qt image by rendered_frame() and then
  // composited by the game's Metal renderer.  It deliberately never becomes
  // a native desktop window, so fullscreen remains uninterrupted.
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
