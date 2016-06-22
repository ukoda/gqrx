/* -*- c++ -*- */
/*
 * Gqrx SDR: Software defined radio receiver powered by GNU Radio and Qt
 *           http://gqrx.dk/
 *
 * Copyright 2011-2013 Alexandru Csete OZ9AEC.
 *
 * Gqrx is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 *
 * Gqrx is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Gqrx; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */
#include <QDebug>
#include <QVariant>
#include "dockrxopt.h"
#include "ui_dockrxopt.h"


QStringList DockRxOpt::ModulationStrings;

// Filter preset table per mode, preset and lo/hi
static const int filter_preset_table[DockRxOpt::MODE_LAST][3][2] =
{   //     WIDE             NORMAL            NARROW
    {{      0,      0}, {     0,     0}, {     0,     0}},  // MODE_OFF
    {{ -25000,  25000}, {-10000, 10000}, { -1000,  1000}},  // MODE_RAW
    {{ -10000,  10000}, { -5000,  5000}, { -2500,  2500}},  // MODE_AM
    {{ -10000,  10000}, { -5000,  5000}, { -2500,  2500}},  // MODE_NFM
    {{-100000, 100000}, {-80000, 80000}, {-60000, 60000}},  // MODE_WFM_MONO
    {{-100000, 100000}, {-80000, 80000}, {-60000, 60000}},  // MODE_WFM_STEREO
    {{  -4000,   -100}, { -2800,  -100}, { -1600,  -200}},  // MODE_LSB
    {{    100,   4000}, {   100,  2800}, {   200,  1600}},  // MODE_USB
    {{  -1000,   1000}, {  -250,   250}, {  -100,   100}},  // MODE_CWL
    {{  -1000,   1000}, {  -250,   250}, {  -100,   100}},  // MODE_CWU
    {{-100000, 100000}, {-80000, 80000}, {-60000, 60000}}   // MODE_WFM_STEREO_OIRT
};

DockRxOpt::DockRxOpt(qint64 filterOffsetRange, QWidget *parent) :
    QDockWidget(parent),
    ui(new Ui::DockRxOpt),
    agc_is_on(true),
    hw_freq_hz(144500000)
{
    ui->setupUi(this);

    if (ModulationStrings.size() == 0)
    {
        // Keep in sync with rxopt_mode_idx
        ModulationStrings.append("Demod Off");
        ModulationStrings.append("Raw I/Q");
        ModulationStrings.append("AM");
        ModulationStrings.append("Narrow FM");
        ModulationStrings.append("WFM (mono)");
        ModulationStrings.append("WFM (stereo)");
        ModulationStrings.append("LSB");
        ModulationStrings.append("USB");
        ModulationStrings.append("CW-L");
        ModulationStrings.append("CW-U");
        ModulationStrings.append("WFM (oirt)");
    }
    ui->modeSelector->addItems(ModulationStrings);

#ifdef Q_OS_MAC
    // Workaround for Mac, see http://stackoverflow.com/questions/3978889/why-is-qhboxlayout-causing-widgets-to-overlap
    // Might be fixed in Qt 5?
    ui->modeButton->setAttribute(Qt::WA_LayoutUsesWidgetRect);
    ui->agcButton->setAttribute(Qt::WA_LayoutUsesWidgetRect);
    ui->autoSquelchButton->setAttribute(Qt::WA_LayoutUsesWidgetRect);
#endif

#ifdef Q_OS_LINUX
    ui->modeButton->setMinimumSize(32, 24);
    ui->agcButton->setMinimumSize(32, 24);
    ui->autoSquelchButton->setMinimumSize(32, 24);
    ui->nbOptButton->setMinimumSize(32, 24);
    ui->nb2Button->setMinimumSize(32, 24);
    ui->nb1Button->setMinimumSize(32, 24);
#endif

    ui->filterFreq->setup(7, -filterOffsetRange/2, filterOffsetRange/2, 1, UNITS_KHZ);
    ui->filterFreq->setFrequency(0);

    // use same slot for filteCombo and filterShapeCombo
    connect(ui->filterShapeCombo, SIGNAL(activated(int)), this, SLOT(on_filterCombo_activated(int)));

    // demodulator options dialog
    demodOpt = new CDemodOptions(this);
    demodOpt->setCurrentPage(CDemodOptions::PAGE_FM_OPT);
    connect(demodOpt, SIGNAL(fmMaxdevSelected(float)), this, SLOT(demodOpt_fmMaxdevSelected(float)));
    connect(demodOpt, SIGNAL(fmEmphSelected(double)), this, SLOT(demodOpt_fmEmphSelected(double)));
    connect(demodOpt, SIGNAL(amDcrToggled(bool)), this, SLOT(demodOpt_amDcrToggled(bool)));
    connect(demodOpt, SIGNAL(cwOffsetChanged(int)), this, SLOT(demodOpt_cwOffsetChanged(int)));

    // AGC options dialog
    agcOpt = new CAgcOptions(this);
    connect(agcOpt, SIGNAL(gainChanged(int)), this, SLOT(agcOpt_gainChanged(int)));
    connect(agcOpt, SIGNAL(thresholdChanged(int)), this, SLOT(agcOpt_thresholdChanged(int)));
    connect(agcOpt, SIGNAL(decayChanged(int)), this, SLOT(agcOpt_decayChanged(int)));
    connect(agcOpt, SIGNAL(slopeChanged(int)), this, SLOT(agcOpt_slopeChanged(int)));
    connect(agcOpt, SIGNAL(hangChanged(bool)), this, SLOT(agcOpt_hangToggled(bool)));

    // Noise blanker options
    nbOpt = new CNbOptions(this);
    connect(nbOpt, SIGNAL(thresholdChanged(int,double)), this, SLOT(nbOpt_thresholdChanged(int,double)));
}

DockRxOpt::~DockRxOpt()
{
    delete ui;
    delete demodOpt;
    delete agcOpt;
    delete nbOpt;
}

/**
 * @brief Set value of channel filter offset selector.
 * @param freq_hz The frequency in Hz
 */
void DockRxOpt::setFilterOffset(qint64 freq_hz)
{
    ui->filterFreq->setFrequency(freq_hz);
}

/**
 * @brief Set filter offset range.
 * @param range_hz The new range in Hz.
 */
void DockRxOpt::setFilterOffsetRange(qint64 range_hz)
{
    if (range_hz > 0)
        ui->filterFreq->setup(7, -range_hz/2, range_hz/2, 1, UNITS_KHZ);
}

/**
 * @brief Set new RF frequency
 * @param freq_hz The frequency in Hz
 *
 * RF frequency is the frequency to which the device device is tuned to
 * The actual RX frequency is the sum of the RF frequency and the filter
 * offset.
 */
void DockRxOpt::setHwFreq(qint64 freq_hz)
{
    hw_freq_hz = freq_hz;
    updateHwFreq();
}

/** Update RX frequency label. */
void DockRxOpt::updateHwFreq()
{
    double hw_freq_mhz = hw_freq_hz / 1.0e6;
    ui->hwFreq->setText(QString("%1 MHz").arg(hw_freq_mhz, 11, 'f', 6, ' '));
}

/**
 * Get filter index from filter LO / HI values.
 * @param lo The filter low cut frequency.
 * @param hi The filter high cut frequency.
 *
 * Given filter low and high cut frequencies, this function checks whether the
 * filter settings correspond to one of the presets in filter_preset_table and
 * returns the corresponding index to ui->filterCombo;
 */
unsigned int DockRxOpt::filterIdxFromLoHi(int lo, int hi) const
{
    int mode_index = ui->modeSelector->currentIndex();

    if (lo == filter_preset_table[mode_index][FILTER_PRESET_WIDE][0] &&
        hi == filter_preset_table[mode_index][FILTER_PRESET_WIDE][1])
        return FILTER_PRESET_WIDE;
    else if (lo == filter_preset_table[mode_index][FILTER_PRESET_NORMAL][0] &&
             hi == filter_preset_table[mode_index][FILTER_PRESET_NORMAL][1])
        return FILTER_PRESET_NORMAL;
    else if (lo == filter_preset_table[mode_index][FILTER_PRESET_NARROW][0] &&
             hi == filter_preset_table[mode_index][FILTER_PRESET_NARROW][1])
        return FILTER_PRESET_NARROW;

    return FILTER_PRESET_USER;
}

/**
 * @brief Set filter parameters
 * @param lo Low cutoff frequency in Hz
 * @param hi High cutoff frequency in Hz.
 *
 * This function will automatically select te "User" preset in the
 * combo box.
 */
void DockRxOpt::setFilterParam(int lo, int hi)
{
    int filter_index = filterIdxFromLoHi(lo, hi);

    ui->filterCombo->setCurrentIndex(filter_index);
    if (filter_index == FILTER_PRESET_USER)
    {
        float width_f;
        width_f = fabs((hi-lo)/1000.f);
        ui->filterCombo->setItemText(FILTER_PRESET_USER, QString("User (%1 k)")
                                     .arg(width_f));
    }
}

/**
 * @brief Select new filter preset.
 * @param index Index of the new filter preset (0=wide, 1=normal, 2=narrow).
 */
void DockRxOpt::setCurrentFilter(int index)
{
    ui->filterCombo->setCurrentIndex(index);
}

/**
 * @brief Get current filter preset.
 * @param The current filter preset (0=wide, 1=normal, 2=narrow).
 */
int  DockRxOpt::currentFilter()
{
    return ui->filterCombo->currentIndex();
}

/** Select filter shape */
void DockRxOpt::setCurrentFilterShape(int index)
{
    ui->filterShapeCombo->setCurrentIndex(index);
}

int  DockRxOpt::currentFilterShape()
{
    return ui->filterShapeCombo->currentIndex();
}


/**
 * @brief Select new demodulator.
 * @param demod Demodulator index corresponding to receiver::demod.
 */
void DockRxOpt::setCurrentDemod(int demod)
{
    if ((demod >= MODE_OFF) && (demod < MODE_LAST))
    {
        ui->modeSelector->setCurrentIndex(demod);
        updateDemodOptPage(demod);
    }
}


/**
 * @brief Get current demodulator selection.
 * @return The current demodulator corresponding to receiver::demod.
 */
int  DockRxOpt::currentDemod()
{
    return ui->modeSelector->currentIndex();
}

QString DockRxOpt::currentDemodAsString()
{
    return GetStringForModulationIndex(currentDemod());
}

float DockRxOpt::currentMaxdev()
{
    qDebug() << __FILE__ << __FUNCTION__ << "FIXME";
    return 5000.0;
}

/**
 * @brief Select new scan mode.
 * @param index Index of the new scan mode (0=off, 1=strongest, 2=newest).
 */
void DockRxOpt::setScanMode(int index)
{
    ui->scanModeCombo->setCurrentIndex(index);
}

/**
 * @brief Get current scan mode.
 * @param The current filter preset (0=off, 1=strongest, 2=newest).
 */
int  DockRxOpt::scanMode()
{
    return ui->scanModeCombo->currentIndex();
}

/**
 * @brief Set squelch level.
 * @param level Squelch level in dBFS
 */
void DockRxOpt::setSquelchLevel(double level)
{
    ui->sqlSpinBox->setValue(level);
}


/** Get filter lo/hi for a given mode and preset */
void DockRxOpt::getFilterPreset(int mode, int preset, int * lo, int * hi) const
{
    if (mode < 0 || mode >= MODE_LAST)
    {
        qDebug() << __func__ << ": Invalid mode:" << mode;
        mode = MODE_AM;
    }
    else if (preset < 0 || preset > 2)
    {
        qDebug() << __func__ << ": Invalid preset:" << preset;
        preset = FILTER_PRESET_NORMAL;
    }
    *lo = filter_preset_table[mode][preset][0];
    *hi = filter_preset_table[mode][preset][1];
}

int DockRxOpt::getCwOffset() const
{
    return demodOpt->getCwOffset();
}

/** Read receiver configuration from settings data. */
void DockRxOpt::readSettings(QSettings *settings)
{
    bool conv_ok;
    int intVal;

    intVal = settings->value("receiver/demod", 0).toInt(&conv_ok);
    if (intVal >= 0)
    {
        setCurrentDemod(intVal);
        emit demodSelected(intVal);
    }

    intVal = settings->value("receiver/cwoffset", 700).toInt(&conv_ok);
    if (conv_ok)
    {
        demodOpt->setCwOffset(intVal);
        //demodOpt_cwOffsetChanged(intVal);
    }

    qint64 offs = settings->value("receiver/offset", 0).toInt(&conv_ok);
    if (offs)
    {
        setFilterOffset(offs);
        emit filterOffsetChanged(offs);
    }

    double dblVal = settings->value("receiver/sql_level", 1.0).toDouble(&conv_ok);
    if (conv_ok && dblVal < 1.0)
    {
        //ui->sqlSlider->setValue(intVal); // signal emitted automatically
        ui->sqlSpinBox->setValue(dblVal);
    }
}

/** Save receiver configuration to settings. */
void DockRxOpt::saveSettings(QSettings *settings)
{
    settings->setValue("receiver/demod", ui->modeSelector->currentIndex());

    int cwofs = demodOpt->getCwOffset();
    if (cwofs == 700)
        settings->remove("receiver/cwoffset");
    else
        settings->setValue("receiver/cwoffset", cwofs);

    qint64 offs = ui->filterFreq->getFrequency();
    if (offs)
        settings->setValue("receiver/offset", offs);
    else
        settings->remove("receiver/offset");

    qDebug() << __func__ << "*** FIXME_ SQL on/off";
    //int sql_lvl = double(ui->sqlSlider->value());  // note: dBFS*10 as int
    double sql_lvl = ui->sqlSpinBox->value();
    if (sql_lvl > -150.0)
        settings->setValue("receiver/sql_level", sql_lvl);
    else
        settings->remove("receiver/sql_level");
}

/**
 * @brief Channel filter offset has changed
 * @param freq The new filter offset in Hz
 *
 * This slot is activated when a new filter offset has been selected either
 * usig the mouse or using the keyboard.
 */
void DockRxOpt::on_filterFreq_newFrequency(qint64 freq)
{
    qDebug() << "New filter offset:" << freq << "Hz";
    updateHwFreq();

    emit filterOffsetChanged(freq);
}

/**
 * New filter preset selected.
 *
 * Instead of implementing a new signal, we simply emit demodSelected() since
 * demodulator and filter preset are tightly coupled.
 */
void DockRxOpt::on_filterCombo_activated(int index)
{
    Q_UNUSED(index);

    qDebug() << "New filter preset:" << ui->filterCombo->currentText();
    qDebug() << "            shape:" << ui->filterShapeCombo->currentIndex();
    emit demodSelected(ui->modeSelector->currentIndex());
}

/**
 * @brief Mode selector activated.
 * @param New mode selection.
 *
 * This slot is activated when the user selects a new demodulator (mode change).
 * It is connected automatically by the UI constructor, and it emits the demodSelected()
 * signal.
 *
 * Note that the modes listed in the selector are different from those defined by
 * receiver::demod (we want to list LSB/USB separately but they have identical demods).
 */
void DockRxOpt::on_modeSelector_activated(int index)
{
    qDebug() << "New mode: " << index;

    if (index == MODE_RAW)
    {
        qDebug() << "Raw I/Q not implemented (fallback to FM-N)";
        ui->modeSelector->setCurrentIndex(MODE_NFM);
        emit demodSelected(MODE_NFM);
        return;
    }

    updateDemodOptPage(index);

    emit demodSelected(index);
}

void DockRxOpt::updateDemodOptPage(int demod)
{
    // update demodulator option widget
    if (demod == MODE_NFM)
        demodOpt->setCurrentPage(CDemodOptions::PAGE_FM_OPT);
    else if (demod == MODE_AM)
        demodOpt->setCurrentPage(CDemodOptions::PAGE_AM_OPT);
    else if (demod == MODE_CWL || demod == MODE_CWU)
        demodOpt->setCurrentPage(CDemodOptions::PAGE_CW_OPT);
    else
        demodOpt->setCurrentPage(CDemodOptions::PAGE_NO_OPT);
}

/** Show demodulator options. */
void DockRxOpt::on_modeButton_clicked()
{
    demodOpt->show();
}

/** Show AGC options. */
void DockRxOpt::on_agcButton_clicked()
{
    agcOpt->show();
}

/**
 * @brief Auto-squelch button clicked.
 *
 * This slot is called when the user clicks on the auto-squelch button.
 */
void DockRxOpt::on_autoSquelchButton_clicked()
{
    // Emit signal
    double newval = sqlAutoClicked(); // FIXME: We rely on signal only being connected to one slot
    ui->sqlSpinBox->setValue(newval);
}

/** AGC preset has changed. */
void DockRxOpt::on_agcPresetCombo_activated(int index)
{
    CAgcOptions::agc_preset_e preset = (CAgcOptions::agc_preset_e) index;

    switch (preset)
    {
    case CAgcOptions::AGC_FAST:
    case CAgcOptions::AGC_MEDIUM:
    case CAgcOptions::AGC_SLOW:
    case CAgcOptions::AGC_USER:
        if (!agc_is_on)
        {
            emit agcToggled(true);
            agc_is_on = true;
        }
        agcOpt->setPreset(preset);
        break;

    case CAgcOptions::AGC_OFF:
        if (agc_is_on)
        {
            emit agcToggled(false);
            agc_is_on = false;
        }
        agcOpt->setPreset(preset);
        break;

    default:
        qDebug() << "Invalid AGC preset:" << index;
    }
}

void DockRxOpt::agcOpt_hangToggled(bool checked)
{
    qDebug() << "AGC hang" << (checked ? "ON" : "OFF");
    emit agcHangToggled(checked);
}

/**
 * @brief AGC threshold ("knee") changed.
 * @param value The new AGC threshold in dB.
 */
void DockRxOpt::agcOpt_thresholdChanged(int value)
{
    qDebug() << "AGC threshold:" << value;
    emit agcThresholdChanged(value);
}

/**
 * @brief AGC slope factor changed.
 * @param value The new slope factor in dB.
 */
void DockRxOpt::agcOpt_slopeChanged(int value)
{
    qDebug() << "AGC slope:" << value;
    emit agcSlopeChanged(value);
}

/**
 * @brief AGC decay changed.
 * @param value The new decay rate in ms (tbc).
 */
void DockRxOpt::agcOpt_decayChanged(int value)
{
    qDebug() << "AGC decay:" << value;
    emit agcDecayChanged(value);
}

/**
 * @brief AGC manual gain changed.
 * @param gain The new gain in dB.
 */
void DockRxOpt::agcOpt_gainChanged(int gain)
{
    qDebug() << "AGC manual gain:" << gain;
    emit agcGainChanged(gain);
}

/**
 * @brief Squelch level change.
 * @param value The new squelch level in dB.
 */
void DockRxOpt::on_sqlSpinBox_valueChanged(double value)
{
    emit sqlLevelChanged(value);
}

/**
 * @brief FM deviation changed by user.
 * @param max_dev The new deviation in Hz.
 */
void DockRxOpt::demodOpt_fmMaxdevSelected(float max_dev)
{
    emit fmMaxdevSelected(max_dev);
}

/**
 * @brief FM de-emphasis changed by user.
 * @param tau The new time constant in uS.
 */
void DockRxOpt::demodOpt_fmEmphSelected(double tau)
{
    emit fmEmphSelected(tau);
}

/**
 * @brief AM DC removal toggled by user.
 * @param enabled Whether DCR is enabled or not.
 */
void DockRxOpt::demodOpt_amDcrToggled(bool enabled)
{
    emit amDcrToggled(enabled);
}

void DockRxOpt::demodOpt_cwOffsetChanged(int offset)
{
    emit cwOffsetChanged(offset);
}

/** Noise blanker 1 button has been toggled. */
void DockRxOpt::on_nb1Button_toggled(bool checked)
{
    emit noiseBlankerChanged(1, checked, (float) nbOpt->nbThreshold(1));
}

/** Noise blanker 2 button has been toggled. */
void DockRxOpt::on_nb2Button_toggled(bool checked)
{
    emit noiseBlankerChanged(2, checked, (float) nbOpt->nbThreshold(2));
}

/** Noise blanker threshold has been changed. */
void DockRxOpt::nbOpt_thresholdChanged(int nbid, double value)
{
    if (nbid == 1)
        emit noiseBlankerChanged(nbid, ui->nb1Button->isChecked(), (float) value);
    else
        emit noiseBlankerChanged(nbid, ui->nb2Button->isChecked(), (float) value);
}

void DockRxOpt::on_nbOptButton_clicked()
{
    nbOpt->show();
}

int DockRxOpt::GetEnumForModulationString(QString param)
{
    int iModulation = -1;
    for(int i=0; i<DockRxOpt::ModulationStrings.size(); ++i)
    {
        QString& strModulation = DockRxOpt::ModulationStrings[i];
        if(param.compare(strModulation, Qt::CaseInsensitive)==0)
        {
            iModulation = i;
            break;
        }
    }
    if(iModulation == -1)
    {
        printf("Modulation '%s' is unknown.\n", param.toStdString().c_str());
        iModulation = MODE_OFF;
    }
    return iModulation;
}

bool DockRxOpt::IsModulationValid(QString strModulation)
{
    return DockRxOpt::ModulationStrings.contains(strModulation, Qt::CaseInsensitive);
}

QString DockRxOpt::GetStringForModulationIndex(int iModulationIndex)
{
    return ModulationStrings[iModulationIndex];
}
