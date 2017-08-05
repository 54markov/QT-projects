#include "cpumonitor.h"

#include <stdlib.h>
#include <iostream>
#include <fstream>
#include <streambuf>
#include <string>
#include <chrono>
#include <thread>

#include <qcustomplot/qcustomplot.h>
#include <cpuid/libcpuid.h>

CpuMonitor::CpuMonitor(Ui::MainWindow* ui)
{
    this->userInterface_ = ui;
}

CpuMonitor::~CpuMonitor()
{
    // TODO
}

void CpuMonitor::hwInfoGet()
{
    struct cpu_raw_data_t raw;
    struct cpu_id_t data;

    try {     
        if (!cpuid_present())
            throw "Your CPU doesn't support CPUID!";

        if (cpuid_get_raw_data(&raw) < 0)
            throw "Can't get the CPUID raw data";

        if (cpu_identify(&raw, &data) < 0)
            throw "CPU identification failed";

#if __x86_64__
        this->arch_        = "x86-64" ;
#else
        this->arch_        = "x86" ;
#endif
        this->vendor_      = data.vendor_str;
        this->codeName_    = data.cpu_codename;
        this->brandName_   = data.brand_str;
        this->l1cache_     = data.l1_data_cache;
        this->l2cache_     = data.l2_cache;
        this->hwCoresNum_  = data.num_cores;
        this->logcoresNum_ = data.num_logical_cpus;

        this->minFrequency_ = 0;
        this->maxFrequency_ = readCpuFrequencyMax();
    } catch (std::string err) {
        std::cerr << err << std::endl;
        exit(1); // TODO
    }
}

void CpuMonitor::hwInfoShow()
{
    this->userInterface_->Arch_txt->setText(this->arch_);
    this->userInterface_->Vendor_txt->setText(this->vendor_);
    this->userInterface_->Model_txt->setText(this->codeName_);
    this->userInterface_->Brand_txt->setText(this->brandName_);

    this->userInterface_->HwCores_txt->setText(QString::number(this->hwCoresNum_));
    this->userInterface_->LogCores_txt->setText(QString::number(this->logcoresNum_));
    this->userInterface_->L1cache_txt->setText(QString::number(this->l1cache_));
    this->userInterface_->L2cache_txt->setText(QString::number(this->l2cache_));
}

void CpuMonitor::hwUsageGather(bool activate)
{
    if (!activate)
        return;

    this->createGraph();
    this->connectSignalSlot();
}

void CpuMonitor::hwUsageShow()
{
    static QTime time(QTime::currentTime());
    static double lastTimePoint = 0;

    // Calculate two new data points:
    double timePoint = time.elapsed()/1000.0;

    if (timePoint - lastTimePoint > 0.100) // Every 100 ms
    {
        this->readCpuFrequencyUsage();
        for (auto i = 0; i < this->hwCoresNum_; ++i) {
            this->userInterface_->CpuUsageGraph->graph(i)->addData(timePoint, this->currFreqUsage_[i]);
        }
        lastTimePoint = timePoint;
    }

    this->userInterface_->CpuUsageGraph->xAxis->setRange(timePoint, 8, Qt::AlignRight);
    this->userInterface_->CpuUsageGraph->replot();
}

void CpuMonitor::createGraph()
{
    auto minFrequency = 0;
    auto maxFrequency = this->maxFrequency_ + 1000;

    this->userInterface_->CpuUsageGraph->axisRect()->setupFullAxesBox();
    this->userInterface_->CpuUsageGraph->yAxis->setRange(minFrequency, maxFrequency);
    this->userInterface_->CpuUsageGraph->yAxis->setLabel("Frequency, Mhz");
    this->userInterface_->CpuUsageGraph->xAxis->setLabel("Time, sec");

    // TODO
    for (auto i = 0; i != this->hwCoresNum_; ++i) {
        this->currFreqUsage_.push_back(0.0);
        this->userInterface_->CpuUsageGraph->addGraph();
        switch (i)
        {
            case 0: this->userInterface_->CpuUsageGraph->graph(i)->setPen(QPen(QColor(0, 0, 255))); break;
            case 1: this->userInterface_->CpuUsageGraph->graph(i)->setPen(QPen(QColor(0, 255, 0))); break;
            case 2: this->userInterface_->CpuUsageGraph->graph(i)->setPen(QPen(QColor(255, 0, 0))); break;
            case 3: this->userInterface_->CpuUsageGraph->graph(i)->setPen(QPen(QColor(150, 0, 255))); break;
            case 4: this->userInterface_->CpuUsageGraph->graph(i)->setPen(QPen(QColor(0, 150, 255))); break;
            case 5: this->userInterface_->CpuUsageGraph->graph(i)->setPen(QPen(QColor(255, 0, 150))); break;
            case 6: this->userInterface_->CpuUsageGraph->graph(i)->setPen(QPen(QColor(255, 200, 200))); break;
            case 7: this->userInterface_->CpuUsageGraph->graph(i)->setPen(QPen(QColor(10, 250, 200))); break;
            default: this->userInterface_->CpuUsageGraph->graph(i)->setPen(QPen(QColor(0, 0, 0))); break;
        }
    }
}

void CpuMonitor::connectSignalSlot()
{
    QObject::connect(this->userInterface_->CpuUsageGraph->xAxis, SIGNAL(rangeChanged(QCPRange)),
            this->userInterface_->CpuUsageGraph->xAxis2, SLOT(setRange(QCPRange)));

    QObject::connect(this->userInterface_->CpuUsageGraph->yAxis, SIGNAL(rangeChanged(QCPRange)),
            this->userInterface_->CpuUsageGraph->yAxis2, SLOT(setRange(QCPRange)));
}

void CpuMonitor::readCpuFrequencyUsage()
{
    std::ifstream sysFile("/proc/cpuinfo", std::ios::in);
    if (sysFile.is_open()) {
        std::string line;
        while(getline(sysFile, line)) {
            this->parseCpuFrequencyUsage(line);
        }
        sysFile.close();
    } else {
        throw "Can't read proc cpu";
    }
}

uint32_t CpuMonitor::readCpuFrequencyMax()
{
    uint32_t maxFrequency = 10000000;
    std::ifstream sysFile("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq", std::ios::in);
    if (sysFile.is_open()) {
        std::string line;
        getline(sysFile, line);
        maxFrequency = stoi(line);
        sysFile.close();
    }
    return maxFrequency / 1000;
}

void CpuMonitor::parseCpuFrequencyUsage(std::string& str)
{
    static auto coreNumber = 0;

    std::size_t found = str.find("MHz");
    if (found == std::string::npos)
        return;

    found = str.find(":");
    if (found == std::string::npos)
        return;

    for (auto i = found + 1; i < str.size(); ++i) {
        if (str[i] != ' ') {
            found = i;
            break;
        }
    }
    str.erase(str.begin(), str.begin() + found);
    this->currFreqUsage_[coreNumber++] = stod(str);
    if (coreNumber > this->hwCoresNum_) {
        coreNumber = 0;
    }
}
