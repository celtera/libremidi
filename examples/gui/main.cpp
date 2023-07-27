#include <libremidi/configurations.hpp>
#include <libremidi/libremidi.hpp>

#include <QApplication>
#include <QHBoxLayout>
#include <QListWidget>
#include <QMessageBox>
#include <QSplitter>
#include <QWidget>

Q_DECLARE_METATYPE(libremidi::port_information);

int main(int argc, char** argv)
{
  using namespace libremidi;
  auto in_api = libremidi::midi_in_default_configuration();
  auto out_api = libremidi::midi_out_default_configuration();
  auto observer_api = libremidi::observer_default_configuration();

  // Create the GUI
  QApplication app{argc, argv};
  QSplitter main;
  QListWidget inputs, outputs, messages;
  main.addWidget(&inputs);
  main.addWidget(&messages);
  main.addWidget(&outputs);

  main.show();

  std::map<libremidi::port_information, QListWidgetItem*> input_items;
  std::map<libremidi::port_information, QListWidgetItem*> output_items;

  // Define the observer callbacks which will fill the list widgets with the input & output devices
  observer_configuration conf{
      .input_added =
          [&](const port_information& p) {
    auto item = new QListWidgetItem{QString::fromStdString(p.display_name)};
    item->setData(Qt::UserRole, QVariant::fromValue(p));
    input_items[p] = item;

    inputs.addItem(item);
      },
      .input_removed =
          [&](const port_information& p) {
    if (auto it = input_items.find(p); it != input_items.end())
    {
      inputs.removeItemWidget(it->second);
      input_items.erase(it);
    }
      },
      .output_added
      = [&](const port_information& p) {
    auto item = new QListWidgetItem{QString::fromStdString(p.display_name)};
    item->setData(Qt::UserRole, QVariant::fromValue(p));
    output_items[p] = item;

    outputs.addItem(item);
      },
      .output_removed = [&](const port_information& p) {
        if (auto it = output_items.find(p); it != output_items.end())
        {
          outputs.removeItemWidget(it->second);
          output_items.erase(it);
        }
      }};

  // Create the libremidi structures
  observer obs{conf, observer_api};
  libremidi::midi_out out{{}, out_api};

  auto input_callback = [&](const libremidi::message& m) {
    // We move things to the main thread as the API may run its callbacks in some random thread
    QMetaObject::invokeMethod(qApp, [&, m] {
      QString msg;
      msg += QString::number(m.timestamp);
      msg += ": ";
      msg += QString::number(m.bytes.size());
      msg += ": ";

      for (auto byte : m.bytes)
      {
        msg += QString::number(byte, 16);
        msg += ' ';
      }
      messages.addItem(msg);
      if (messages.count() > 10)
        delete messages.takeItem(0);

      // Forward to the output port
      if (out.is_port_open())
      {
        out.send_message(m);
      }
    });
  };

  libremidi::midi_in in{{.on_message = input_callback}, in_api};

  // Connect gui changes to port changes
  QObject::connect(
      &inputs, &QListWidget::currentItemChanged, [&](QListWidgetItem* selected, QListWidgetItem*) {
        in.close_port();
        for (auto& [port, item] : input_items)
        {
          if (item == selected)
          {
            in.open_port(port);
            if (!in.is_port_open())
            {
              QMessageBox::warning(
                  &main, QString("Error !"),
                  QString("Could not connect to input:\n%1\n%2")
                      .arg(port.display_name.c_str())
                      .arg(port.port_name.c_str()));
            }
            return;
          }
        }
      });

  QObject::connect(
      &outputs, &QListWidget::currentItemChanged,
      [&](QListWidgetItem* selected, QListWidgetItem*) {
    out.close_port();
    for (auto& [port, item] : output_items)
    {
      if (item == selected)
      {
        out.open_port(port);
        if (!out.is_port_open())
        {
          QMessageBox::warning(
              &main, QString("Error !"),
              QString("Could not connect to output:\n%1\n%2")
                  .arg(port.display_name.c_str())
                  .arg(port.port_name.c_str()));
        }
        return;
      }
    }
      });
  return app.exec();
}
