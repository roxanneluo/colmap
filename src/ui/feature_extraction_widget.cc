// COLMAP - Structure-from-Motion and Multi-View Stereo.
// Copyright (C) 2016  Johannes L. Schoenberger <jsch at inf.ethz.ch>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include "ui/feature_extraction_widget.h"

#include "base/camera_models.h"
#include "base/feature_extraction.h"
#include "ui/options_widget.h"
#include "ui/qt_utils.h"
#include "ui/thread_control_widget.h"

namespace colmap {

class ExtractionWidget : public OptionsWidget {
 public:
  ExtractionWidget(QWidget* parent, OptionManager* options);

  virtual void Run() = 0;

 protected:
  OptionManager* options_;
  ThreadControlWidget* thread_control_widget_;
};

class SIFTExtractionWidget : public ExtractionWidget {
 public:
  SIFTExtractionWidget(QWidget* parent, OptionManager* options);

  void Run() override;

 private:
  QRadioButton* sift_gpu_;
  QRadioButton* sift_cpu_;
};

class ImportFeaturesWidget : public ExtractionWidget {
 public:
  ImportFeaturesWidget(QWidget* parent, OptionManager* options);

  void Run() override;

 private:
  std::string import_path_;
};

ExtractionWidget::ExtractionWidget(QWidget* parent, OptionManager* options)
    : OptionsWidget(parent),
      options_(options),
      thread_control_widget_(new ThreadControlWidget(this)) {}

SIFTExtractionWidget::SIFTExtractionWidget(QWidget* parent,
                                           OptionManager* options)
    : ExtractionWidget(parent, options) {
  sift_gpu_ = new QRadioButton(tr("GPU"), this);
  sift_gpu_->setChecked(true);
  grid_layout_->addWidget(sift_gpu_);
  grid_layout_->addWidget(sift_gpu_, grid_layout_->rowCount(), 1);

  sift_cpu_ = new QRadioButton(tr("CPU"), this);
  grid_layout_->addWidget(sift_cpu_, grid_layout_->rowCount(), 1);

  AddSpacer();

  SiftOptions& sift_options = options->extraction_options->sift;
  AddOptionInt(&sift_options.max_image_size, "max_image_size");
  AddOptionInt(&sift_options.max_num_features, "max_num_features");
  AddOptionInt(&sift_options.first_octave, "first_octave", -5);
  AddOptionInt(&sift_options.num_octaves, "num_octaves");
  AddOptionInt(&sift_options.octave_resolution, "octave_resolution");
  AddOptionDouble(&sift_options.peak_threshold, "peak_threshold", 0.0, 1e7,
                  0.0001, 4);
  AddOptionDouble(&sift_options.edge_threshold, "edge_threshold");
  AddOptionInt(&sift_options.max_num_orientations, "max_num_orientations");
  AddOptionBool(&sift_options.upright, "upright");

  SiftGPUFeatureExtractor::Options& gpu_options =
      options->extraction_options->gpu;
  AddOptionInt(&gpu_options.index, "gpu_index", -1);

  SiftCPUFeatureExtractor::Options& cpu_options =
      options->extraction_options->cpu;
  AddOptionInt(&cpu_options.num_threads, "cpu_num_threads", -1);
  AddOptionInt(&cpu_options.batch_size_factor, "cpu_batch_size_factor");
}

void SIFTExtractionWidget::Run() {
  WriteOptions();

  ImageReader::Options reader_options = options_->extraction_options->reader;
  reader_options.database_path = *options_->database_path;
  reader_options.image_path = *options_->image_path;

  Thread* extractor = nullptr;
  if (sift_gpu_->isChecked()) {
    extractor = new SiftGPUFeatureExtractor(reader_options,
                                            options_->extraction_options->sift,
                                            options_->extraction_options->gpu);
  } else {
    extractor = new SiftCPUFeatureExtractor(reader_options,
                                            options_->extraction_options->sift,
                                            options_->extraction_options->cpu);
  }

  thread_control_widget_->StartThread("Extracting...", true, extractor);
}

ImportFeaturesWidget::ImportFeaturesWidget(QWidget* parent,
                                           OptionManager* options)
    : ExtractionWidget(parent, options) {
  AddOptionDirPath(&import_path_, "import_path");
}

void ImportFeaturesWidget::Run() {
  WriteOptions();

  if (!boost::filesystem::is_directory(import_path_)) {
    QMessageBox::critical(this, "", tr("Path is not a directory"));
    return;
  }

  ImageReader::Options reader_options = options_->extraction_options->reader;
  reader_options.database_path = *options_->database_path;
  reader_options.image_path = *options_->image_path;

  Thread* importer = new FeatureImporter(reader_options, import_path_);
  thread_control_widget_->StartThread("Importing...", true, importer);
}

FeatureExtractionWidget::FeatureExtractionWidget(QWidget* parent,
                                                 OptionManager* options)
    : parent_(parent), options_(options) {
  // Do not change flag, to make sure feature database is not accessed from
  // multiple threads
  setWindowFlags(Qt::Window);
  setWindowTitle("Feature extraction");

  QGridLayout* grid = new QGridLayout(this);

  grid->addWidget(CreateCameraModelBox(), 0, 0);

  tab_widget_ = new QTabWidget(this);
  tab_widget_->addTab(new SIFTExtractionWidget(this, options), tr("Extract"));
  tab_widget_->addTab(new ImportFeaturesWidget(this, options), tr("Import"));
  grid->addWidget(tab_widget_);

  QPushButton* extract_button = new QPushButton(tr("Extract"), this);
  connect(extract_button, &QPushButton::released, this,
          &FeatureExtractionWidget::Extract);
  grid->addWidget(extract_button, grid->rowCount(), 0);
}

QGroupBox* FeatureExtractionWidget::CreateCameraModelBox() {
  camera_model_ids_.clear();

  camera_model_cb_ = new QComboBox(this);

#define CAMERA_MODEL_CASE(CameraModel)                                     \
  camera_model_cb_->addItem(                                               \
      QString::fromStdString(CameraModelIdToName(CameraModel::model_id))); \
  camera_model_ids_.push_back(static_cast<int>(CameraModel::model_id));

  CAMERA_MODEL_CASES

#undef CAMERA_MODEL_CASE

  camera_params_exif_rb_ =
      new QRadioButton(tr("Camera parameters from EXIF"), this);
  camera_params_exif_rb_->setChecked(true);

  camera_params_custom_rb_ =
      new QRadioButton(tr("Custom camera parameters"), this);

  camera_params_info_ = new QLabel(tr(""), this);
  QPalette pal = QPalette(camera_params_info_->palette());
  pal.setColor(QPalette::WindowText, QColor(130, 130, 130));
  camera_params_info_->setPalette(pal);
  camera_params_info_->setAlignment(Qt::AlignRight);

  camera_params_text_ = new QLineEdit(this);
  camera_params_text_->setEnabled(false);
  camera_params_text_->setAlignment(Qt::AlignRight);

  single_camera_cb_ = new QCheckBox("Single camera for all images", this);
  single_camera_cb_->setChecked(false);

  QGroupBox* box = new QGroupBox(tr("Camera model"), this);

  QVBoxLayout* vbox = new QVBoxLayout(box);
  vbox->addWidget(camera_model_cb_);
  vbox->addWidget(camera_params_exif_rb_);
  vbox->addWidget(camera_params_custom_rb_);
  vbox->addWidget(camera_params_info_);
  vbox->addWidget(camera_params_text_);
  vbox->addWidget(single_camera_cb_);
  vbox->addStretch(1);

  box->setLayout(vbox);

  SelectCameraModel(camera_model_cb_->currentIndex());

  connect(camera_model_cb_,
          (void (QComboBox::*)(int)) & QComboBox::currentIndexChanged, this,
          &FeatureExtractionWidget::SelectCameraModel);
  connect(camera_params_exif_rb_, &QRadioButton::clicked, camera_params_text_,
          &QLineEdit::setDisabled);
  connect(camera_params_custom_rb_, &QRadioButton::clicked, camera_params_text_,
          &QLineEdit::setEnabled);

  return box;
}

void FeatureExtractionWidget::showEvent(QShowEvent* event) {
  parent_->setDisabled(true);
  ReadOptions();
}

void FeatureExtractionWidget::hideEvent(QHideEvent* event) {
  parent_->setEnabled(true);
  WriteOptions();
}

void FeatureExtractionWidget::ReadOptions() {
  const auto camera_code =
      CameraModelNameToId(options_->extraction_options->reader.camera_model);
  for (size_t i = 0; i < camera_model_ids_.size(); ++i) {
    if (camera_model_ids_[i] == camera_code) {
      SelectCameraModel(i);
      camera_model_cb_->setCurrentIndex(i);
      break;
    }
  }
  single_camera_cb_->setChecked(
      options_->extraction_options->reader.single_camera);
  camera_params_text_->setText(QString::fromStdString(
      options_->extraction_options->reader.camera_params));
}

void FeatureExtractionWidget::WriteOptions() {
  options_->extraction_options->reader.camera_model =
      CameraModelIdToName(camera_model_ids_[camera_model_cb_->currentIndex()]);
  options_->extraction_options->reader.single_camera =
      single_camera_cb_->isChecked();
  options_->extraction_options->reader.camera_params =
      camera_params_text_->text().toUtf8().constData();
}

void FeatureExtractionWidget::SelectCameraModel(const int idx) {
  const int code = camera_model_ids_[idx];
  camera_params_info_->setText(
      QString::fromStdString(CameraModelParamsInfo(code)));
}

void FeatureExtractionWidget::Extract() {
  // If the custom parameter radiobuttion is not checked, but the
  // parameters textbox contains parameters.
  const auto old_camera_params_text = camera_params_text_->text();
  if (!camera_params_custom_rb_->isChecked()) {
    camera_params_text_->setText("");
  }

  WriteOptions();

  const std::vector<double> camera_params =
      CSVToVector<double>(options_->extraction_options->reader.camera_params);
  const auto camera_code =
      CameraModelNameToId(options_->extraction_options->reader.camera_model);

  if (camera_params_custom_rb_->isChecked() &&
      !CameraModelVerifyParams(camera_code, camera_params)) {
    QMessageBox::critical(this, "", tr("Invalid camera parameters"));
    return;
  }

  static_cast<ExtractionWidget*>(tab_widget_->currentWidget())->Run();

  camera_params_text_->setText(old_camera_params_text);
}

}  // namespace colmap
