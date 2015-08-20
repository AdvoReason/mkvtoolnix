#include "common/common_pch.h"

#include "common/debugging.h"
#include "common/qt.h"
#include "mkvtoolnix-gui/jobs/mux_job.h"
#include "mkvtoolnix-gui/jobs/tool.h"
#include "mkvtoolnix-gui/main_window/main_window.h"
#include "mkvtoolnix-gui/merge/command_line_dialog.h"
#include "mkvtoolnix-gui/merge/tab.h"
#include "mkvtoolnix-gui/merge/tool.h"
#include "mkvtoolnix-gui/forms/main_window/main_window.h"
#include "mkvtoolnix-gui/forms/merge/tab.h"
#include "mkvtoolnix-gui/util/message_box.h"
#include "mkvtoolnix-gui/util/option_file.h"
#include "mkvtoolnix-gui/util/settings.h"
#include "mkvtoolnix-gui/util/util.h"
#include "mkvtoolnix-gui/watch_jobs/tool.h"

#include <QComboBox>
#include <QMenu>
#include <QTreeView>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QString>
#include <QTimer>

namespace mtx { namespace gui { namespace Merge {

using namespace mtx::gui;

Tab::Tab(QWidget *parent)
  : QWidget{parent}
  , ui{new Ui::Tab}
  , m_filesModel{new SourceFileModel{this}}
  , m_tracksModel{new TrackModel{this}}
  , m_currentlySettingInputControlValues{false}
  , m_addFilesAction{new QAction{this}}
  , m_appendFilesAction{new QAction{this}}
  , m_addAdditionalPartsAction{new QAction{this}}
  , m_removeFilesAction{new QAction{this}}
  , m_removeAllFilesAction{new QAction{this}}
  , m_selectAllTracksAction{new QAction{this}}
  , m_enableAllTracksAction{new QAction{this}}
  , m_disableAllTracksAction{new QAction{this}}
  , m_selectAllVideoTracksAction{new QAction{this}}
  , m_selectAllAudioTracksAction{new QAction{this}}
  , m_selectAllSubtitlesTracksAction{new QAction{this}}
  , m_filesMenu{new QMenu{this}}
  , m_tracksMenu{new QMenu{this}}
  , m_attachmentsMenu{new QMenu{this}}
  , m_selectTracksOfTypeMenu{new QMenu{this}}
  , m_attachmentsModel{new AttachmentModel{this}}
  , m_addAttachmentsAction{new QAction{this}}
  , m_removeAttachmentsAction{new QAction{this}}
  , m_removeAllAttachmentsAction{new QAction{this}}
  , m_selectAllAttachmentsAction{new QAction{this}}
  , m_debugTrackModel{"track_model"}
{
  // Setup UI controls.
  ui->setupUi(this);

  auto mw = MainWindow::get();
  connect(mw, &MainWindow::preferencesChanged, this, &Tab::setupTabPositions);

  m_filesModel->setTracksModel(m_tracksModel);

  setupInputControls();
  setupOutputControls();
  setupAttachmentsControls();
  setupTabPositions();

  setControlValuesFromConfig();

  retranslateUi();

  Util::fixScrollAreaBackground(ui->scrollArea);
  Util::preventScrollingWithoutFocus(this);

  m_savedState = currentState();
}

Tab::~Tab() {
}

QString const &
Tab::fileName()
  const {
  return m_config.m_configFileName;
}

QString
Tab::title()
  const {
  auto title = m_config.m_destination.isEmpty() ? QY("<no output file>") : QFileInfo{m_config.m_destination}.fileName();
  if (!m_config.m_configFileName.isEmpty())
    title = Q("%1 (%2)").arg(title).arg(QFileInfo{m_config.m_configFileName}.fileName());

  return title;
}

void
Tab::onShowCommandLine() {
  auto options = (QStringList{} << Util::Settings::get().actualMkvmergeExe()) + updateConfigFromControlValues().buildMkvmergeOptions();
  CommandLineDialog{this, options, QY("mkvmerge command line")}.exec();
}

void
Tab::load(QString const &fileName) {
  try {
    m_config.load(fileName);
    setControlValuesFromConfig();

    m_savedState = currentState();

    MainWindow::get()->setStatusBarMessage(QY("The configuration has been loaded."));

    emit titleChanged();

  } catch (InvalidSettingsX &) {
    m_config.reset();

    Util::MessageBox::critical(this, QY("Error loading settings file"), QY("The settings file '%1' contains invalid settings and was not loaded.").arg(fileName));

    emit removeThisTab();
  }
}

void
Tab::cloneConfig(MuxConfig const &config) {
  m_config = config;

  setControlValuesFromConfig();

  m_config.m_configFileName.clear();
  m_savedState.clear();

  emit titleChanged();
}

void
Tab::onSaveConfig() {
  if (m_config.m_configFileName.isEmpty()) {
    onSaveConfigAs();
    return;
  }

  updateConfigFromControlValues();
  m_config.save();

  m_savedState = currentState();

  MainWindow::get()->setStatusBarMessage(QY("The configuration has been saved."));
}

void
Tab::onSaveOptionFile() {
  auto &settings = Util::Settings::get();
  auto fileName  = QFileDialog::getSaveFileName(this, QY("Save option file"), settings.m_lastConfigDir.path(), QY("All files") + Q(" (*)"));
  if (fileName.isEmpty())
    return;

  Util::OptionFile::create(fileName, updateConfigFromControlValues().buildMkvmergeOptions());
  settings.m_lastConfigDir = QFileInfo{fileName}.path();
  settings.save();

  MainWindow::get()->setStatusBarMessage(QY("The option file has been created."));
}

void
Tab::onSaveConfigAs() {
  auto &settings = Util::Settings::get();
  auto fileName  = QFileDialog::getSaveFileName(this, QY("Save settings file as"), settings.m_lastConfigDir.path(), QY("MKVToolnix GUI config files") + Q(" (*.mtxcfg);;") + QY("All files") + Q(" (*)"));
  if (fileName.isEmpty())
    return;

  updateConfigFromControlValues();
  m_config.save(fileName);
  settings.m_lastConfigDir = QFileInfo{fileName}.path();
  settings.save();

  m_savedState = currentState();

  emit titleChanged();

  MainWindow::get()->setStatusBarMessage(QY("The configuration has been saved."));
}

void
Tab::onAddToJobQueue() {
  addToJobQueue(false);
}

void
Tab::onStartMuxing() {
  addToJobQueue(true);
}

QString
Tab::getOpenFileName(QString const &title,
                     QString const &filter,
                     QLineEdit *lineEdit) {
  auto fullFilter = filter;
  if (!fullFilter.isEmpty())
    fullFilter += Q(";;");
  fullFilter += QY("All files") + Q(" (*)");

  auto &settings = Util::Settings::get();
  auto dir       = lineEdit->text().isEmpty() ? settings.m_lastOpenDir.path() : QFileInfo{ lineEdit->text() }.path();
  auto fileName  = QFileDialog::getOpenFileName(this, title, dir, fullFilter);
  if (fileName.isEmpty())
    return fileName;

  settings.m_lastOpenDir = QFileInfo{fileName}.path();
  settings.save();

  lineEdit->setText(fileName);

  return fileName;
}


QString
Tab::getSaveFileName(QString const &title,
                     QString const &filter,
                     QLineEdit *lineEdit) {
  auto fullFilter = filter;
  if (!fullFilter.isEmpty())
    fullFilter += Q(";;");
  fullFilter += QY("All files") + Q(" (*)");

  auto &settings = Util::Settings::get();
  auto dir       = !lineEdit->text().isEmpty()                                                               ? lineEdit->text()
                 : !settings.m_lastOutputDir.path().isEmpty() && (settings.m_lastOutputDir.path() != Q(".")) ? settings.m_lastOutputDir.path()
                 :                                                                                             settings.m_lastOpenDir.path();
  auto fileName  = QFileDialog::getSaveFileName(this, title, dir, fullFilter);
  if (fileName.isEmpty())
    return fileName;

  settings.m_lastOutputDir = QFileInfo{fileName}.path();
  settings.save();

  lineEdit->setText(fileName);

  return fileName;
}

void
Tab::setControlValuesFromConfig() {
  m_filesModel->setSourceFiles(m_config.m_files);
  m_tracksModel->setTracks(m_config.m_tracks);
  m_attachmentsModel->replaceAttachments(m_config.m_attachments);

  resizeFilesColumnsToContents();
  resizeTracksColumnsToContents();
  resizeAttachmentsColumnsToContents();

  onTrackSelectionChanged();
  setOutputControlValues();
  onAttachmentSelectionChanged();
}

MuxConfig &
Tab::updateConfigFromControlValues() {
  m_config.m_attachments = m_attachmentsModel->attachments();

  return m_config;
}

void
Tab::retranslateUi() {
  ui->retranslateUi(this);

  retranslateInputUI();
  retranslateOutputUI();
  retranslateAttachmentsUI();

  emit titleChanged();
}

bool
Tab::isReadyForMerging() {
  if (m_config.m_files.isEmpty()) {
    Util::MessageBox::critical(this, QY("Cannot start merging"), QY("You have to add at least one source file before you can start merging or add a job to the job queue."));
    return false;
  }

  if (m_config.m_destination.isEmpty()) {
    Util::MessageBox::critical(this, QY("Cannot start merging"), QY("You have to set the output file name before you can start merging or add a job to the job queue."));
    return false;
  }

  return true;
}

void
Tab::addToJobQueue(bool startNow) {
  updateConfigFromControlValues();

  if (!isReadyForMerging())
    return;

  auto &cfg      = Util::Settings::get();
  auto newConfig = std::make_shared<MuxConfig>(m_config);
  auto job       = std::make_shared<Jobs::MuxJob>(startNow ? Jobs::Job::PendingAuto : Jobs::Job::PendingManual, newConfig);

  job->setDateAdded(QDateTime::currentDateTime());
  job->setDescription(job->displayableDescription());

  if (!startNow) {
    if (!cfg.m_useDefaultJobDescription) {
      auto newDescription = QString{};

      while (newDescription.isEmpty()) {
        bool ok = false;
        newDescription = QInputDialog::getText(this, QY("Enter job description"), QY("Please enter the new job's description."), QLineEdit::Normal, job->description(), &ok);
        if (!ok)
          return;
      }

      job->setDescription(newDescription);
    }

    MainWindow::get()->showIconMovingToTool(Q("task-delegate.png"), *MainWindow::jobTool());

  } else {
    if (cfg.m_switchToJobOutputAfterStarting)
      MainWindow::get()->switchToTool(MainWindow::watchJobTool());
    else
      MainWindow::get()->showIconMovingToTool(Q("media-playback-start.png"), *MainWindow::watchJobTool());
  }

  MainWindow::jobTool()->addJob(std::static_pointer_cast<Jobs::Job>(job));

  m_savedState = currentState();

  handleClearingMergeSettings();
}

void
Tab::handleClearingMergeSettings() {
  auto action = Util::Settings::get().m_clearMergeSettings;
  if (Util::Settings::ClearMergeSettingsAction::None == action)
    return;

  if (Util::Settings::ClearMergeSettingsAction::RemoveInputFiles == action) {
    onRemoveAllFiles();
    return;
  }

  // Util::Settings::ClearMergeSettingsAction::NewSettings
  MainWindow::mergeTool()->newConfig();
  emit removeThisTab();
}

QString
Tab::currentState() {
  updateConfigFromControlValues();
  return m_config.toString();
}

bool
Tab::hasBeenModified() {
  return currentState() != m_savedState;
}

void
Tab::setupTabPositions() {
  ui->tabs->setTabPosition(Util::Settings::get().m_tabPosition);
}

}}}
