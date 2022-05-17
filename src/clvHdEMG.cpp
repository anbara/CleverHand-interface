#include "clvHdEMG.hpp"
#define LOG(...)             \
    if(m_verbose)            \
    {                        \
        printf(__VA_ARGS__); \
        fflush(stdout);      \
    }

namespace ClvHd
{

EMG::EMG(Master *master, int id) : m_master(master), m_id(id)
{
    for(int i = 0; i < 0x50; i++) m_regs[i] = 0x00;

    m_regs[CONFIG_REG] = 0x02;
    m_regs[LOD_CN_REG] = 0x08;
    m_regs[AFE_PACE_CN_REG] = 0x01;
    m_regs[DIGO_STRENGTH_REG] = 0x03;
    m_regs[R2_RATE_REG] = 0x08;
    m_regs[R3_RATE_CH1_REG] = 0x80;
    m_regs[R3_RATE_CH2_REG] = 0x80;
    m_regs[R3_RATE_CH3_REG] = 0x80;
    m_regs[SYNCB_CN_REG] = 0x40;
    m_regs[RESERVED_0x2D_REG] = 0x09;
    m_regs[ALARM_FILTER_REG] = 0x33;
    m_regs[REVID_REG] = 0x01;

    m_fast_value = (int16_t *)(m_regs + DATA_CH1_PACE_REG);
    for(int i = 0; i < 3; i++)
        m_precise_value[i] = (int32_t *)(m_regs + DATA_CH1_ECG_REG + 3 * i);

    m_fast_adc_max = 0x8000;
    for(int i = 0; i < 3; i++) { m_precise_adc_max[i] = 0x800000; }
};

EMG::~EMG() { m_master->writeReg(m_id, CONFIG_REG, 0x02); }

void
EMG::setup()
{
    uint8_t val = 0;
    m_master->readReg(m_id, 0x40, 1, &val);
    LOG("> version %d\n", val);

    CLK_SRC clk_src = INTERN; //Start clk and output on CLK pin (0x05);
    //route channel1 (+)in1 & (-)in6 (0x31)
    int route_table[3][2] = {{1, 6}, {0, 0}, {0, 0}};
    //enable ch1 and disaable ch2 and ch3 (0x36)
    bool chx_enable[3] = {true, false, false};
    //no high resolution & Clock frequency for Channel 1 :204800 (0x08)
    bool chx_high_res[3] = {true, false, false};
    bool chx_high_freq[3] = {true, false, false};
    int R1[3] = {2, 4, 4}; //R1 ch1:2 ch2:4 ch3:4 (0x1)
    int R2 = 2;            //R2 = 4 (0x01)
    int R3[3] = {2, 2, 2}; //R3_ch1 = 4 (0x01)

    // standby mode (0x02)
    this->set_mode(STANDBY);
    this->config_clock(true, clk_src, true);
    this->route_channel(1, route_table[0][0], route_table[0][1]);
    this->enable_channels(chx_enable[0], chx_enable[1], chx_enable[2]);
    this->config_resolution(chx_high_res[0], chx_high_res[1], chx_high_res[2]);
    this->config_frequence(chx_high_freq[0], chx_high_freq[1],
                           chx_high_freq[2]);
    this->config_R1(R1[0], R1[1], R1[2]);
    this->config_R2(R2);
    this->config_R3(1, R3[0]);

    // m_master->writeReg(m_id, FLEX_CH1_CN_REG, 0b01000000);
    //m_master->writeReg(m_id, FLEX_VBAT_CN_REG, 0b1);

    val = m_master->readReg(m_id, R1_RATE_REG, 1, &val);
    LOG("> version %d\n", val);

    LOG("> Seting up ADS1293 (%d):\n", m_id);
    LOG("> Set clock on CLK %s pin\n",
        (clk_src == INTERN) ? "intern" : "extern");
    LOG("> route ch1:(-)%d (+)%d\n", route_table[0][0], route_table[0][1]);
    LOG("> Ch1(%sabled) Ch2(%sabled) Ch3(%sabled)\n",
        chx_enable[0] ? "en" : "dis", chx_enable[1] ? "en" : "dis",
        chx_enable[2] ? "en" : "dis");
    LOG("> Resolution Ch1(%s) Ch2(%s) Ch3(%s)\n",
        chx_high_res[0] ? "high" : "low", chx_high_res[1] ? "high" : "low",
        chx_high_res[2] ? "high" : "low");
    LOG("> Precision Ch1(%s) Ch2(%s) Ch3(%s)\n",
        chx_high_freq[0] ? "high" : "low", chx_high_freq[1] ? "high" : "low",
        chx_high_freq[2] ? "high" : "low");
    LOG("> R1 Ch1(%d) Ch2(%d) Ch3(%d)\n", R1[0], R1[1], R1[2]);
    LOG("> R2 %d\n", R2);
    LOG("> R3 Ch1(%d) Ch2(%d) Ch3(%d)\n", R3[0], R3[1], R3[2]);

    // check DIS_EFILTER: 0x26

    //Start conversion (0x01)
    this->set_mode(START_CONV);

    LOG("> Start conversion\n");
    LOG("> Reading errors registers:\n");

    this->get_error();
    this->error_status_str(m_regs[ERROR_STATUS_REG]);
    LOG("> %s\n", this->error_status_str(m_regs[ERROR_STATUS_REG]).c_str());
    LOG("> %s\n", this->error_range_str(m_regs[ERROR_RANGE1_REG]).c_str());
    LOG("> %d\n", m_regs[ERROR_STATUS_REG]);
    LOG("> %d\n", m_regs[ERROR_RANGE1_REG]);

    val = m_master->readReg(m_id, 0x19, 1, &val);
    LOG("> %d\n", val);
    //m_master->printBit(val);
    val = m_master->readReg(m_id, 0x1a, 1, &val);
    LOG("> %d\n", val);
    //m_master->printBit(val);
}

void
EMG::route_channel(uint8_t channel, uint8_t pos_in, uint8_t neg_in)
{
    pos_in = (pos_in < 0) ? 0 : (pos_in > 6) ? 6 : pos_in;
    neg_in = (neg_in < 0) ? 0 : (neg_in > 6) ? 6 : neg_in;

    channel = ((channel < 1) ? 1 : (channel > 3) ? 3 : channel) - 1;
    uint8_t val = pos_in | (neg_in << 3);
    m_regs[FLEX_CH1_CN_REG + channel] = val;
    m_master->writeReg(m_id, FLEX_CH1_CN_REG + channel, val);
}

int
EMG::set_mode(Mode mode)
{
    m_mode = mode;
    m_regs[CONFIG_REG] = mode;
    return m_master->writeReg(m_id, CONFIG_REG, mode);
}

void
EMG::config_clock(bool start, CLK_SRC src, bool en_output)
{
    uint8_t val = (start ? 0x4 : 0x0) | (src << 1) | (en_output ? 0x1 : 0x0);
    m_regs[OSC_CN_REG] = val;
    m_master->writeReg(m_id, OSC_CN_REG, val);
}

void
EMG::enable_channels(bool ch1, bool ch2, bool ch3)
{
    uint8_t val =
        (ch1 ? 0 : 0b001001) | (ch2 ? 0 : 0b010010) | (ch3 ? 0 : 0b100100);
    m_regs[AFE_SHDN_CN_REG] = val;
    m_master->writeReg(m_id, AFE_SHDN_CN_REG, val);
}

void
EMG::enable_SDM(bool ch1, bool ch2, bool ch3)
{
    uint8_t val = (m_regs[AFE_SHDN_CN_REG] & 0b111) | (ch1 ? 0 : 0b1000) |
                  (ch2 ? 0 : 0b10000) | (ch3 ? 0 : 0b100000);
    m_regs[AFE_SHDN_CN_REG] = val;
    m_master->writeReg(m_id, AFE_SHDN_CN_REG, val);
}

void
EMG::enable_INA(bool ch1, bool ch2, bool ch3)
{
    uint8_t val = (m_regs[AFE_SHDN_CN_REG] & 0b111000) | (ch1 ? 0 : 0b1) |
                  (ch2 ? 0 : 0b10) | (ch3 ? 0 : 0b100);
    m_regs[AFE_SHDN_CN_REG] = val;
    m_master->writeReg(m_id, AFE_SHDN_CN_REG, val);
}

void
EMG::config_resolution(bool ch1_high_res, bool ch2_high_res, bool ch3_high_res)
{
    uint8_t val = (m_regs[AFE_RES_REG] & 0b00111000) |
                  (ch1_high_res ? 0b1 : 0) | (ch2_high_res ? 0b010 : 0) |
                  (ch3_high_res ? 0b100 : 0);
    m_regs[AFE_RES_REG] = val;
    m_master->writeReg(m_id, AFE_RES_REG, val);
}

void
EMG::config_frequence(bool ch1_freq_double,
                      bool ch2_freq_double,
                      bool ch3_freq_double)
{
    uint8_t val =
        (m_regs[AFE_RES_REG] & 0b111) | (ch1_freq_double ? 0b1000 : 0) |
        (ch2_freq_double ? 0b010000 : 0) | (ch3_freq_double ? 0b100000 : 0);
    m_regs[AFE_RES_REG] = val;
    m_master->writeReg(m_id, AFE_RES_REG, val);
}

void
EMG::config_R1(uint8_t R1_ch1, uint8_t R1_ch2, uint8_t R1_ch3)
{
    R1_ch1 = (R1_ch1 < 3) ? 0b001 : 0; //2 or 4
    R1_ch2 = (R1_ch2 < 3) ? 0b010 : 0; //2 or 4
    R1_ch3 = (R1_ch3 < 3) ? 0b100 : 0; //2 or 4
    uint8_t val = R1_ch1 | R1_ch1 | R1_ch1;
    m_regs[R1_RATE_REG] = val;
    m_master->writeReg(m_id, R1_RATE_REG, 0x01);
}

void
EMG::config_R2(uint8_t R2)
{
    bool R3_val =
        (m_regs[R3_RATE_CH1_REG] == 0b10 || m_regs[R3_RATE_CH1_REG] == 0b1000)
            ? false
            : true;
    if(R2 < 5)
    {
        R2 = 0b0001; //4
        m_fast_adc_max = 0x8000;
        for(int i = 0; i < 3; i++)
            m_precise_adc_max[i] = R3_val ? 0x800000 : 0xF30000;
    }
    else if(R2 < 6)
    {
        R2 = 0b0010; //5
        m_fast_adc_max = 0xC350;
        for(int i = 0; i < 3; i++)
            m_precise_adc_max[i] = R3_val ? 0xC35000 : 0xB964F0;
    }
    else if(R2 < 8)
    {
        R2 = 0b0100; //6
        m_fast_adc_max = 0xF300;
        for(int i = 0; i < 3; i++)
            m_precise_adc_max[i] = R3_val ? 0xF30000 : 0xE6A900;
    }
    else
    {
        R2 = 0b1000; //8
        m_fast_adc_max = 0x8000;
        for(int i = 0; i < 3; i++)
            m_precise_adc_max[i] = R3_val ? 0x800000 : 0xF30000;
    }
    m_regs[R2_RATE_REG] = R2;
    m_master->writeReg(m_id, R2_RATE_REG, R2);
}

void
EMG::config_R3(int ch, uint8_t R3)
{
    if(R3 < 6)
        R3 = 0b00000001; //4
    else if(R3 < 8)
        R3 = 0b00000010; //6
    else if(R3 < 11)
        R3 = 0b00000100; //8
    else if(R3 < 15)
        R3 = 0b00001000; //12
    else if(R3 < 25)
        R3 = 0b00010000; //16
    else if(R3 < 49)
        R3 = 0b00100000; //32
    else if(R3 < 97)
        R3 = 0b01000000; //64
    else
        R3 = 0b10000000; //128

    //change conv max adc
    bool R3_val = (R3 == 0b10 || R3 == 0b1000) ? false : true;
    if(m_regs[R2_RATE_REG] == 0b0010)
        m_precise_adc_max[ch - 1] = R3_val ? 0xC35000 : 0xB964F0;
    else if(m_regs[R2_RATE_REG] == 0b0100)
        m_precise_adc_max[ch - 1] = R3_val ? 0xF30000 : 0xE6A900;
    else
        m_precise_adc_max[ch - 1] = R3_val ? 0x800000 : 0xF30000;

    m_regs[R3_RATE_CH1_REG + ch - 1] = R3;
    m_master->writeReg(m_id, R3_RATE_CH1_REG + ch - 1, R3);
}

double
EMG::precise_value(int ch)
{
    return conv(ch, *m_precise_value[ch - 1]);
};

double
EMG::fast_value(int ch)
{

    return conv(m_fast_value[ch - 1]);
};

double
EMG::read_fast_value(int ch)
{
    m_master->readReg(m_id, DATA_CH1_PACE_REG + 2 * (ch - 1), 2,
                      &(m_fast_value[ch - 1]));
    return conv(m_fast_value[ch - 1]);
}

double
EMG::read_precise_value(int ch)
{
    m_master->readReg(m_id, DATA_CH1_ECG_REG + 3 * (ch - 1), 3,
                      m_precise_value[ch - 1]);
    return conv(ch, *m_precise_value[ch - 1]);
}

double
EMG::conv(uint16_t val)
{
    // uint16_t lim = 0x8000;
    // if((uint16_t)val > lim)
     // std::cout << "16conv " << std::hex << __builtin_bswap16(val) << " "
     //           << m_fast_adc_max << std::dec << std::endl;
    // std::cout << "conv " << std::hex << __builtin_bswap16(val) << " "
    //           << std::dec
    //           << (__builtin_bswap16(val) * 1. / m_fast_adc_max - 0.5) *
    //                  4.8 / 3.5 * 1000
    //           << std::endl;
    return (__builtin_bswap16(val) * 1. / m_fast_adc_max - 0.5) * 4.8 / 3.5 *
           1000;
}

double
EMG::conv(int ch, int32_t val)
{
    //m_regs[DATA_CH1_ECG_REG+3] =0x01;
    // std::cout << "32conv " << std::hex << (__builtin_bswap32(val) >> 8) << " "
    //           << m_precise_adc_max[ch - 1] << std::dec << std::endl;
    return ((__builtin_bswap32(val) >> 8) * 1. / m_precise_adc_max[ch - 1] -
            0.5) *
           4.8 / 3.5 * 1000;
}

EMG::Error *
EMG::get_error()
{
    m_master->readReg(m_id, ERROR_LOD_REG, 7, &m_regs[ERROR_LOD_REG]);
    return (Error *)&(m_regs[ERROR_LOD_REG]);
}

std::string
EMG::error_range_str(uint8_t err_byte)
{
    std::string str;
    for(int i = 0; i < 8; i++)
        if(1 & (err_byte >> i))
            switch(i)
            {
            case 0:
                str += "INA output out-of-range |";
                break;
            case 1:
                str += "INA (+) output near (+) rail |";
                break;
            case 2:
                str += "INA (+) output near (-) rail |";
                break;
            case 3:
                str += "INA (-) output near (+) rail |";
                break;
            case 4:
                str += "INA (-) output near (-) rail |";
                break;
            case 6:
                str += "Sigma-delta modulator over range |";
                break;
            }
    return str;
}

std::string
EMG::error_status_str(uint8_t err_byte)
{
    std::string str;
    for(int i = 0; i < 8; i++)
        if(1 & (err_byte >> i))
            switch(i)
            {
            case 0:
                str += "Common-mode level out-of-range |";
                break;
            case 1:
                str += "Right leg drive near rail |";
                break;
            case 2:
                str += "Low battery |";
                break;
            case 3:
                str += "Lead off detected |";
                break;
            case 4:
                str += "Channel 1 out-of-range error |";
                break;
            case 5:
                str += "Channel 2 out-of-range error |";
                break;
            case 6:
                str += "Channel 3 out-of-range error |";
                break;
            case 7:
                str += "Digital synchronization error |";
                break;
            }
    return str;
}

} // namespace ClvHd