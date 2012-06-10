/*
 * Carla Backend
 * Copyright (C) 2011-2012 Filipe Coelho <falktx@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * For a full copy of the GNU General Public License see the COPYING file
 */

#include "carla_plugin.h"

#include "ladspa/ladspa.h"
#include "ladspa_rdf.h"

CARLA_BACKEND_START_NAMESPACE

#if 0
} /* adjust editor indent */
#endif

bool is_rdf_port_good(int Type1, int Type2)
{
    if (LADSPA_IS_PORT_INPUT(Type1) && ! LADSPA_IS_PORT_INPUT(Type2))
        return false;
    else if (LADSPA_IS_PORT_OUTPUT(Type1) && ! LADSPA_IS_PORT_OUTPUT(Type2))
        return false;
    else if (LADSPA_IS_PORT_CONTROL(Type1) && ! LADSPA_IS_PORT_CONTROL(Type2))
        return false;
    else if (LADSPA_IS_PORT_AUDIO(Type1) && ! LADSPA_IS_PORT_AUDIO(Type2))
        return false;
    else
        return true;
}

bool is_ladspa_rdf_descriptor_valid(const LADSPA_RDF_Descriptor* rdf_descriptor, const LADSPA_Descriptor* descriptor)
{
    if (rdf_descriptor)
    {
        if (rdf_descriptor->PortCount <= descriptor->PortCount)
        {
            for (unsigned long i=0; i < rdf_descriptor->PortCount; i++)
            {
                if (is_rdf_port_good(rdf_descriptor->Ports[i].Type, descriptor->PortDescriptors[i]) == false)
                {
                    qWarning("WARNING - Plugin has RDF data, but invalid PortTypes: %i != %i", rdf_descriptor->Ports[i].Type, descriptor->PortDescriptors[i]);
                    return false;
                }
            }
            return true;
        }
        else
        {
            qWarning("WARNING - Plugin has RDF data, but invalid PortCount: %li > %li", rdf_descriptor->PortCount, descriptor->PortCount);
            return false;
        }
    }
    else
        // No RDF Descriptor
        return false;
}

class LadspaPlugin : public CarlaPlugin
{
public:
    LadspaPlugin(unsigned short id) : CarlaPlugin(id)
    {
        qDebug("LadspaPlugin::LadspaPlugin()");
        m_type = PLUGIN_LADSPA;

        handle = nullptr;
        descriptor = nullptr;
        rdf_descriptor = nullptr;

        param_buffers = nullptr;
    }

    ~LadspaPlugin()
    {
        qDebug("LadspaPlugin::~LadspaPlugin()");

        if (handle && descriptor && descriptor->deactivate && m_active_before)
            descriptor->deactivate(handle);

        if (handle && descriptor && descriptor->cleanup)
            descriptor->cleanup(handle);

        if (rdf_descriptor)
            ladspa_rdf_free(rdf_descriptor);
    }

    // -------------------------------------------------------------------
    // Information (base)

    PluginCategory category()
    {
        if (rdf_descriptor)
        {
            LADSPA_Properties Category = rdf_descriptor->Type;

            // Specific Types
            if (Category & (LADSPA_CLASS_DELAY|LADSPA_CLASS_REVERB))
                return PLUGIN_CATEGORY_DELAY;
            if (Category & (LADSPA_CLASS_PHASER|LADSPA_CLASS_FLANGER|LADSPA_CLASS_CHORUS))
                return PLUGIN_CATEGORY_MODULATOR;
            if (Category & (LADSPA_CLASS_AMPLIFIER))
                return PLUGIN_CATEGORY_DYNAMICS;
            if (Category & (LADSPA_CLASS_UTILITY|LADSPA_CLASS_SPECTRAL|LADSPA_CLASS_FREQUENCY_METER))
                return PLUGIN_CATEGORY_UTILITY;

            // Pre-set LADSPA Types
            if (LADSPA_IS_PLUGIN_DYNAMICS(Category))
                return PLUGIN_CATEGORY_DYNAMICS;
            if (LADSPA_IS_PLUGIN_AMPLITUDE(Category))
                return PLUGIN_CATEGORY_MODULATOR;
            if (LADSPA_IS_PLUGIN_EQ(Category))
                return PLUGIN_CATEGORY_EQ;
            if (LADSPA_IS_PLUGIN_FILTER(Category))
                return PLUGIN_CATEGORY_FILTER;
            if (LADSPA_IS_PLUGIN_FREQUENCY(Category))
                return PLUGIN_CATEGORY_UTILITY;
            if (LADSPA_IS_PLUGIN_SIMULATOR(Category))
                return PLUGIN_CATEGORY_OTHER;
            if (LADSPA_IS_PLUGIN_TIME(Category))
                return PLUGIN_CATEGORY_DELAY;
            if (LADSPA_IS_PLUGIN_GENERATOR(Category))
                return PLUGIN_CATEGORY_SYNTH;
        }

        return get_category_from_name(m_name);
    }

    long unique_id()
    {
        return descriptor->UniqueID;
    }

    // -------------------------------------------------------------------
    // Information (count)

    uint32_t param_scalepoint_count(uint32_t param_id)
    {
        int32_t rindex = param.data[param_id].rindex;

        bool HasPortRDF = (rdf_descriptor && rindex < (int32_t)rdf_descriptor->PortCount);
        if (HasPortRDF)
            return rdf_descriptor->Ports[rindex].ScalePointCount;
        return 0;
    }

    // -------------------------------------------------------------------
    // Information (per-plugin data)

    double get_parameter_value(uint32_t param_id)
    {
        return param_buffers[param_id];
    }

    double get_parameter_scalepoint_value(uint32_t param_id, uint32_t scalepoint_id)
    {
        int32_t param_rindex = param.data[param_id].rindex;

        bool HasPortRDF = (rdf_descriptor && param_rindex < (int32_t)rdf_descriptor->PortCount);
        if (HasPortRDF)
            return rdf_descriptor->Ports[param_rindex].ScalePoints[scalepoint_id].Value;
        return 0.0;
    }

    void get_label(char* buf_str)
    {
        strncpy(buf_str, descriptor->Label, STR_MAX);
    }

    void get_maker(char* buf_str)
    {
        if (rdf_descriptor && rdf_descriptor->Creator)
            strncpy(buf_str, rdf_descriptor->Creator, STR_MAX);
        else
            strncpy(buf_str, descriptor->Maker, STR_MAX);
    }

    void get_copyright(char* buf_str)
    {
        strncpy(buf_str, descriptor->Copyright, STR_MAX);
    }

    void get_real_name(char* buf_str)
    {
        if (rdf_descriptor && rdf_descriptor->Title)
            strncpy(buf_str, rdf_descriptor->Title, STR_MAX);
        else
            strncpy(buf_str, descriptor->Name, STR_MAX);
    }

    void get_parameter_name(uint32_t param_id, char* buf_str)
    {
        int32_t rindex = param.data[param_id].rindex;
        strncpy(buf_str, descriptor->PortNames[rindex], STR_MAX);
    }

    void get_parameter_symbol(uint32_t param_id, char* buf_str)
    {
        int32_t rindex = param.data[param_id].rindex;

        bool HasPortRDF = (rdf_descriptor && rindex < (int32_t)rdf_descriptor->PortCount);
        if (HasPortRDF)
        {
            LADSPA_RDF_Port Port = rdf_descriptor->Ports[rindex];
            if (LADSPA_PORT_HAS_LABEL(Port.Hints))
            {
                strncpy(buf_str, Port.Label, STR_MAX);
                return;
            }
        }
        *buf_str = 0;
    }

    void get_parameter_unit(uint32_t param_id, char* buf_str)
    {
        int32_t rindex = param.data[param_id].rindex;

        bool HasPortRDF = (rdf_descriptor && rindex < (int32_t)rdf_descriptor->PortCount);
        if (HasPortRDF)
        {
            LADSPA_RDF_Port Port = rdf_descriptor->Ports[rindex];
            if (LADSPA_PORT_HAS_UNIT(Port.Hints))
            {
                switch (Port.Unit)
                {
                case LADSPA_UNIT_DB:
                    strncpy(buf_str, "dB", STR_MAX);
                    return;
                case LADSPA_UNIT_COEF:
                    strncpy(buf_str, "(coef)", STR_MAX);
                    return;
                case LADSPA_UNIT_HZ:
                    strncpy(buf_str, "Hz", STR_MAX);
                    return;
                case LADSPA_UNIT_S:
                    strncpy(buf_str, "s", STR_MAX);
                    return;
                case LADSPA_UNIT_MS:
                    strncpy(buf_str, "ms", STR_MAX);
                    return;
                case LADSPA_UNIT_MIN:
                    strncpy(buf_str, "min", STR_MAX);
                    return;
                }
            }
        }
        *buf_str = 0;
    }

    void get_parameter_scalepoint_label(uint32_t param_id, uint32_t scalepoint_id, char* buf_str)
    {
        int32_t param_rindex = param.data[param_id].rindex;

        bool HasPortRDF = (rdf_descriptor && param_rindex < (int32_t)rdf_descriptor->PortCount);
        if (HasPortRDF)
            strncpy(buf_str, rdf_descriptor->Ports[param_rindex].ScalePoints[scalepoint_id].Label, STR_MAX);
        else
            *buf_str = 0;
    }

    // -------------------------------------------------------------------
    // Set data (plugin-specific stuff)

    void set_parameter_value(uint32_t param_id, double value, bool gui_send, bool osc_send, bool callback_send)
    {
        param_buffers[param_id] = fix_parameter_value(value, param.ranges[param_id]);

        CarlaPlugin::set_parameter_value(param_id, value, gui_send, osc_send, callback_send);
    }

    // -------------------------------------------------------------------
    // Plugin state

    void reload()
    {
        qDebug("LadspaPlugin::reload() - start");

        // Safely disable plugin for reload
        const CarlaPluginScopedDisabler m(this);

        if (x_client->isActive())
            x_client->deactivate();

        // Remove client ports
        remove_client_ports();

        // Delete old data
        delete_buffers();

        uint32_t ains, aouts, params, j;
        ains = aouts = params = 0;

        const unsigned long PortCount = descriptor->PortCount;

        for (unsigned long i=0; i<PortCount; i++)
        {
            const LADSPA_PortDescriptor PortType = descriptor->PortDescriptors[i];
            if (LADSPA_IS_PORT_AUDIO(PortType))
            {
                if (LADSPA_IS_PORT_INPUT(PortType))
                    ains += 1;
                else if (LADSPA_IS_PORT_OUTPUT(PortType))
                    aouts += 1;
            }
            else if (LADSPA_IS_PORT_CONTROL(PortType))
                params += 1;
        }

        if (ains > 0)
        {
            ain.ports    = new CarlaEngineAudioPort*[ains];
            ain.rindexes = new uint32_t[ains];
        }

        if (aouts > 0)
        {
            aout.ports    = new CarlaEngineAudioPort*[aouts];
            aout.rindexes = new uint32_t[aouts];
        }

        if (params > 0)
        {
            param.data    = new ParameterData[params];
            param.ranges  = new ParameterRanges[params];
            param_buffers = new float[params];
        }

        const int port_name_size = CarlaEngine::maxPortNameSize() - 1;
        char port_name[port_name_size];
        bool needs_cin  = false;
        bool needs_cout = false;

        for (unsigned long i=0; i<PortCount; i++)
        {
            const LADSPA_PortDescriptor PortType = descriptor->PortDescriptors[i];
            const LADSPA_PortRangeHint PortHint  = descriptor->PortRangeHints[i];
            bool HasPortRDF = (rdf_descriptor && i < rdf_descriptor->PortCount);

            if (LADSPA_IS_PORT_AUDIO(PortType))
            {
#ifndef BUILD_BRIDGE
                if (carla_options.global_jack_client)
                {
                    strcpy(port_name, m_name);
                    strcat(port_name, ":");
                    strncat(port_name, descriptor->PortNames[i], port_name_size/2);
                }
                else
#endif
                    strncpy(port_name, descriptor->PortNames[i], port_name_size);

                if (LADSPA_IS_PORT_INPUT(PortType))
                {
                    j = ain.count++;
                    ain.ports[j]    = (CarlaEngineAudioPort*)x_client->addPort(port_name, CarlaEnginePortTypeAudio, true);
                    ain.rindexes[j] = i;
                }
                else if (LADSPA_IS_PORT_OUTPUT(PortType))
                {
                    j = aout.count++;
                    aout.ports[j]    = (CarlaEngineAudioPort*)x_client->addPort(port_name, CarlaEnginePortTypeAudio, false);
                    aout.rindexes[j] = i;
                    needs_cin = true;
                }
                else
                    qWarning("WARNING - Got a broken Port (Audio, but not input or output)");
            }
            else if (LADSPA_IS_PORT_CONTROL(PortType))
            {
                j = param.count++;
                param.data[j].index  = j;
                param.data[j].rindex = i;
                param.data[j].hints  = 0;
                param.data[j].midi_channel = 0;
                param.data[j].midi_cc = -1;

                double min, max, def, step, step_small, step_large;

                // min value
                if (LADSPA_IS_HINT_BOUNDED_BELOW(PortHint.HintDescriptor))
                    min = PortHint.LowerBound;
                else
                    min = 0.0;

                // max value
                if (LADSPA_IS_HINT_BOUNDED_ABOVE(PortHint.HintDescriptor))
                    max = PortHint.UpperBound;
                else
                    max = 1.0;

                if (min > max)
                    max = min;
                else if (max < min)
                    min = max;

                if (max - min == 0.0)
                {
                    qWarning("Broken plugin parameter: max - min == 0");
                    max = min + 0.1;
                }

                // default value
                if (HasPortRDF && LADSPA_PORT_HAS_DEFAULT(rdf_descriptor->Ports[i].Hints))
                    def = rdf_descriptor->Ports[i].Default;

                else if (LADSPA_IS_HINT_HAS_DEFAULT(PortHint.HintDescriptor))
                {
                    switch (PortHint.HintDescriptor & LADSPA_HINT_DEFAULT_MASK)
                    {
                    case LADSPA_HINT_DEFAULT_MINIMUM:
                        def = min;
                        break;
                    case LADSPA_HINT_DEFAULT_MAXIMUM:
                        def = max;
                        break;
                    case LADSPA_HINT_DEFAULT_0:
                        def = 0.0;
                        break;
                    case LADSPA_HINT_DEFAULT_1:
                        def = 1.0;
                        break;
                    case LADSPA_HINT_DEFAULT_100:
                        def = 100.0;
                        break;
                    case LADSPA_HINT_DEFAULT_440:
                        def = 440.0;
                        break;
                    case LADSPA_HINT_DEFAULT_LOW:
                        if (LADSPA_IS_HINT_LOGARITHMIC(PortHint.HintDescriptor))
                            def = exp((log(min)*0.75) + (log(max)*0.25));
                        else
                            def = (min*0.75) + (max*0.25);
                        break;
                    case LADSPA_HINT_DEFAULT_MIDDLE:
                        if (LADSPA_IS_HINT_LOGARITHMIC(PortHint.HintDescriptor))
                            def = sqrt(min*max);
                        else
                            def = (min+max)/2;
                        break;
                    case LADSPA_HINT_DEFAULT_HIGH:
                        if (LADSPA_IS_HINT_LOGARITHMIC(PortHint.HintDescriptor))
                            def = exp((log(min)*0.25) + (log(max)*0.75));
                        else
                            def = (min*0.25) + (max*0.75);
                        break;
                    default:
                        if (min < 0.0 && max > 0.0)
                            def = 0.0;
                        else
                            def = min;
                        break;
                    }
                }
                else
                {
                    // no default value
                    if (min < 0.0 && max > 0.0)
                        def = 0.0;
                    else
                        def = min;
                }

                if (def < min)
                    def = min;
                else if (def > max)
                    def = max;

                if (LADSPA_IS_HINT_SAMPLE_RATE(PortHint.HintDescriptor))
                {
                    double sample_rate = get_sample_rate();
                    min *= sample_rate;
                    max *= sample_rate;
                    def *= sample_rate;
                    param.data[j].hints |= PARAMETER_USES_SAMPLERATE;
                }

                if (LADSPA_IS_HINT_TOGGLED(PortHint.HintDescriptor))
                {
                    step = max - min;
                    step_small = step;
                    step_large = step;
                    param.data[j].hints |= PARAMETER_IS_BOOLEAN;
                }
                else if (LADSPA_IS_HINT_INTEGER(PortHint.HintDescriptor))
                {
                    step = 1.0;
                    step_small = 1.0;
                    step_large = 10.0;
                    param.data[j].hints |= PARAMETER_IS_INTEGER;
                }
                else
                {
                    double range = max - min;
                    step = range/100.0;
                    step_small = range/1000.0;
                    step_large = range/10.0;
                }

                if (LADSPA_IS_PORT_INPUT(PortType))
                {
                    param.data[j].type   = PARAMETER_INPUT;
                    param.data[j].hints |= PARAMETER_IS_ENABLED;
                    param.data[j].hints |= PARAMETER_IS_AUTOMABLE;
                    needs_cin = true;
                }
                else if (LADSPA_IS_PORT_OUTPUT(PortType))
                {
                    if (strcmp(descriptor->PortNames[i], "latency") == 0 || strcmp(descriptor->PortNames[i], "_latency") == 0)
                    {
                        min = 0.0;
                        max = get_sample_rate();
                        def = 0.0;
                        step = 1.0;
                        step_small = 1.0;
                        step_large = 1.0;

                        param.data[j].type  = PARAMETER_LATENCY;
                        param.data[j].hints = 0;
                    }
                    else
                    {
                        param.data[j].type   = PARAMETER_OUTPUT;
                        param.data[j].hints |= PARAMETER_IS_ENABLED;
                        param.data[j].hints |= PARAMETER_IS_AUTOMABLE;
                        needs_cout = true;
                    }
                }
                else
                {
                    param.data[j].type = PARAMETER_UNKNOWN;
                    qWarning("WARNING - Got a broken Port (Control, but not input or output)");
                }

                // extra parameter hints
                if (LADSPA_IS_HINT_LOGARITHMIC(PortHint.HintDescriptor))
                    param.data[j].hints |= PARAMETER_IS_LOGARITHMIC;

                // check for scalepoints, require at least 2 to make it useful
                if (HasPortRDF && rdf_descriptor->Ports[i].ScalePointCount > 1)
                    param.data[j].hints |= PARAMETER_USES_SCALEPOINTS;

                param.ranges[j].min = min;
                param.ranges[j].max = max;
                param.ranges[j].def = def;
                param.ranges[j].step = step;
                param.ranges[j].step_small = step_small;
                param.ranges[j].step_large = step_large;

                // Start parameters in their default values
                param_buffers[j] = def;

                descriptor->connect_port(handle, i, &param_buffers[j]);
            }
            else
            {
                // Not Audio or Control
                qCritical("ERROR - Got a broken Port (neither Audio or Control)");
                descriptor->connect_port(handle, i, nullptr);
            }
        }

        if (needs_cin)
        {
#ifndef BUILD_BRIDGE
            if (carla_options.global_jack_client)
            {
                strcpy(port_name, m_name);
                strcat(port_name, ":control-in");
            }
            else
#endif
                strcpy(port_name, "control-in");

            param.port_cin = (CarlaEngineControlPort*)x_client->addPort(port_name, CarlaEnginePortTypeControl, true);
        }

        if (needs_cout)
        {
#ifndef BUILD_BRIDGE
            if (carla_options.global_jack_client)
            {
                strcpy(port_name, m_name);
                strcat(port_name, ":control-out");
            }
            else
#endif
                strcpy(port_name, "control-out");

            param.port_cout = (CarlaEngineControlPort*)x_client->addPort(port_name, CarlaEnginePortTypeControl, false);
        }

        ain.count   = ains;
        aout.count  = aouts;
        param.count = params;

        // plugin checks
        m_hints &= ~(PLUGIN_IS_SYNTH | PLUGIN_USES_CHUNKS | PLUGIN_CAN_DRYWET | PLUGIN_CAN_VOLUME | PLUGIN_CAN_BALANCE);

        if (aouts > 0 && (ains == aouts || ains == 1))
            m_hints |= PLUGIN_CAN_DRYWET;

        if (aouts > 0)
            m_hints |= PLUGIN_CAN_VOLUME;

        if (aouts >= 2 && aouts%2 == 0)
            m_hints |= PLUGIN_CAN_BALANCE;

        x_client->activate();

        qDebug("LadspaPlugin::reload() - end");
    }

    // -------------------------------------------------------------------
    // Plugin processing

    void process(float** ains_buffer, float** aouts_buffer, uint32_t nframes, uint32_t nframesOffset)
    {
        uint32_t i, k;

        double ains_peak_tmp[2]  = { 0.0 };
        double aouts_peak_tmp[2] = { 0.0 };

        CARLA_PROCESS_CONTINUE_CHECK;

        // --------------------------------------------------------------------------------------------------------
        // Input VU

        if (ain.count > 0)
        {
            if (ain.count == 1)
            {
                for (k=0; k < nframes; k++)
                {
                    if (abs_d(ains_buffer[0][k]) > ains_peak_tmp[0])
                        ains_peak_tmp[0] = abs_d(ains_buffer[0][k]);
                }
            }
            else if (ain.count >= 1)
            {
                for (k=0; k < nframes; k++)
                {
                    if (abs_d(ains_buffer[0][k]) > ains_peak_tmp[0])
                        ains_peak_tmp[0] = abs_d(ains_buffer[0][k]);

                    if (abs_d(ains_buffer[1][k]) > ains_peak_tmp[1])
                        ains_peak_tmp[1] = abs_d(ains_buffer[1][k]);
                }
            }
        }

        CARLA_PROCESS_CONTINUE_CHECK;

        // --------------------------------------------------------------------------------------------------------
        // Parameters Input [Automation]

        if (param.port_cin && m_active && m_active_before)
        {
            void* cin_buffer = param.port_cin->getBuffer();

            const CarlaEngineControlEvent* cin_event;
            uint32_t time, n_cin_events = param.port_cin->getEventCount(cin_buffer);

            for (i=0; i < n_cin_events; i++)
            {
                cin_event = param.port_cin->getEvent(cin_buffer, i);

                if (! cin_event)
                    continue;

                time = cin_event->time - nframesOffset;

                if (time >= nframes)
                    continue;

                // Control change
                switch (cin_event->type)
                {
                case CarlaEngineEventControlChange:
                {
                    double value;

                    // Control backend stuff
                    if (cin_event->channel == cin_channel)
                    {
                        if (MIDI_IS_CONTROL_BREATH_CONTROLLER(cin_event->controller) && (m_hints & PLUGIN_CAN_DRYWET) > 0)
                        {
                            value = cin_event->value;
                            set_drywet(value, false, false);
                            postpone_event(PluginPostEventParameterChange, PARAMETER_DRYWET, value);
                            continue;
                        }
                        else if (MIDI_IS_CONTROL_CHANNEL_VOLUME(cin_event->controller) && (m_hints & PLUGIN_CAN_VOLUME) > 0)
                        {
                            value = cin_event->value*127/100;
                            set_volume(value, false, false);
                            postpone_event(PluginPostEventParameterChange, PARAMETER_VOLUME, value);
                            continue;
                        }
                        else if (MIDI_IS_CONTROL_BALANCE(cin_event->controller) && (m_hints & PLUGIN_CAN_BALANCE) > 0)
                        {
                            double left, right;
                            value = cin_event->value/0.5 - 1.0;

                            if (value < 0)
                            {
                                left  = -1.0;
                                right = (value*2)+1.0;
                            }
                            else if (value > 0)
                            {
                                left  = (value*2)-1.0;
                                right = 1.0;
                            }
                            else
                            {
                                left  = -1.0;
                                right = 1.0;
                            }

                            set_balance_left(left, false, false);
                            set_balance_right(right, false, false);
                            postpone_event(PluginPostEventParameterChange, PARAMETER_BALANCE_LEFT, left);
                            postpone_event(PluginPostEventParameterChange, PARAMETER_BALANCE_RIGHT, right);
                            continue;
                        }
                    }

                    // Control plugin parameters
                    for (k=0; k < param.count; k++)
                    {
                        if (param.data[k].midi_channel == cin_event->channel && param.data[k].midi_cc == cin_event->controller && param.data[k].type == PARAMETER_INPUT && (param.data[k].hints & PARAMETER_IS_AUTOMABLE) > 0)
                        {
                            if (param.data[k].hints & PARAMETER_IS_BOOLEAN)
                            {
                                value = cin_event->value < 0.5 ? param.ranges[k].min : param.ranges[k].max;
                            }
                            else
                            {
                                value = cin_event->value * (param.ranges[k].max - param.ranges[k].min) + param.ranges[k].min;

                                if (param.data[k].hints & PARAMETER_IS_INTEGER)
                                    value = rint(value);
                            }

                            set_parameter_value(k, value, false, false, false);
                            postpone_event(PluginPostEventParameterChange, k, value);
                        }
                    }

                    break;
                }

                case CarlaEngineEventAllSoundOff:
                    if (cin_event->channel == cin_channel)
                    {
                        if (descriptor->deactivate)
                            descriptor->deactivate(handle);

                        if (descriptor->activate)
                            descriptor->activate(handle);
                    }
                    break;

                default:
                    break;
                }
            }
        } // End of Parameters Input

        CARLA_PROCESS_CONTINUE_CHECK;

        // --------------------------------------------------------------------------------------------------------
        // Special Parameters

#if 0
        for (k=0; k < param.count; k++)
        {
            if (param.data[k].type == PARAMETER_LATENCY)
            {
                // TODO
            }
        }

        CARLA_PROCESS_CONTINUE_CHECK;
#endif

        // --------------------------------------------------------------------------------------------------------
        // Plugin processing

        if (m_active)
        {
            if (! m_active_before)
            {
                if (descriptor->activate)
                    descriptor->activate(handle);
            }

            for (i=0; i < ain.count; i++)
                descriptor->connect_port(handle, ain.rindexes[i], ains_buffer[i]);

            for (i=0; i < aout.count; i++)
                descriptor->connect_port(handle, aout.rindexes[i], aouts_buffer[i]);

            if (descriptor->run)
                descriptor->run(handle, nframes);
        }
        else
        {
            if (m_active_before)
            {
                if (descriptor->deactivate)
                    descriptor->deactivate(handle);
            }
        }

        CARLA_PROCESS_CONTINUE_CHECK;

        // --------------------------------------------------------------------------------------------------------
        // Post-processing (dry/wet, volume and balance)

        if (m_active)
        {
            bool do_drywet  = (m_hints & PLUGIN_CAN_DRYWET) > 0 && x_drywet != 1.0;
            bool do_volume  = (m_hints & PLUGIN_CAN_VOLUME) > 0 && x_vol != 1.0;
            bool do_balance = (m_hints & PLUGIN_CAN_BALANCE) > 0 && (x_bal_left != -1.0 || x_bal_right != 1.0);

            double bal_rangeL, bal_rangeR;
            float old_bal_left[do_balance ? nframes : 0];

            for (i=0; i < aout.count; i++)
            {
                // Dry/Wet and Volume
                if (do_drywet || do_volume)
                {
                    for (k=0; k<nframes; k++)
                    {
                        if (do_drywet)
                        {
                            if (aout.count == 1)
                                aouts_buffer[i][k] = (aouts_buffer[i][k]*x_drywet)+(ains_buffer[0][k]*(1.0-x_drywet));
                            else
                                aouts_buffer[i][k] = (aouts_buffer[i][k]*x_drywet)+(ains_buffer[i][k]*(1.0-x_drywet));
                        }

                        if (do_volume)
                            aouts_buffer[i][k] *= x_vol;
                    }
                }

                // Balance
                if (do_balance)
                {
                    if (i%2 == 0)
                        memcpy(&old_bal_left, aouts_buffer[i], sizeof(float)*nframes);

                    bal_rangeL = (x_bal_left+1.0)/2;
                    bal_rangeR = (x_bal_right+1.0)/2;

                    for (k=0; k<nframes; k++)
                    {
                        if (i%2 == 0)
                        {
                            // left output
                            aouts_buffer[i][k]  = old_bal_left[k]*(1.0-bal_rangeL);
                            aouts_buffer[i][k] += aouts_buffer[i+1][k]*(1.0-bal_rangeR);
                        }
                        else
                        {
                            // right
                            aouts_buffer[i][k]  = aouts_buffer[i][k]*bal_rangeR;
                            aouts_buffer[i][k] += old_bal_left[k]*bal_rangeL;
                        }
                    }
                }

                // Output VU
                for (k=0; k < nframes && i < 2; k++)
                {
                    if (abs_d(aouts_buffer[i][k]) > aouts_peak_tmp[i])
                        aouts_peak_tmp[i] = abs_d(aouts_buffer[i][k]);
                }
            }
        }
        else
        {
            // disable any output sound if not active
            for (i=0; i < aout.count; i++)
                memset(aouts_buffer[i], 0.0f, sizeof(float)*nframes);

            aouts_peak_tmp[0] = 0.0;
            aouts_peak_tmp[1] = 0.0;

        } // End of Post-processing

        CARLA_PROCESS_CONTINUE_CHECK;

        // --------------------------------------------------------------------------------------------------------
        // Control Output

        if (param.port_cout && m_active)
        {
            void* cout_buffer = param.port_cout->getBuffer();

            if (nframesOffset == 0 || ! m_active_before)
                param.port_cout->initBuffer(cout_buffer);

            double value;

            for (k=0; k < param.count; k++)
            {
                if (param.data[k].type == PARAMETER_OUTPUT)
                {
                    fix_parameter_value(param_buffers[k], param.ranges[k]);

                    if (param.data[k].midi_cc > 0)
                    {
                        value = (param_buffers[k] - param.ranges[k].min) / (param.ranges[k].max - param.ranges[k].min);
                        param.port_cout->writeEvent(cout_buffer, CarlaEngineEventControlChange, nframesOffset, param.data[k].midi_channel, param.data[k].midi_cc, value);
                    }
                }
            }
        } // End of Control Output

        CARLA_PROCESS_CONTINUE_CHECK;

        // --------------------------------------------------------------------------------------------------------
        // Peak Values

        ains_peak[(m_id*2)+0]  = ains_peak_tmp[0];
        ains_peak[(m_id*2)+1]  = ains_peak_tmp[1];
        aouts_peak[(m_id*2)+0] = aouts_peak_tmp[0];
        aouts_peak[(m_id*2)+1] = aouts_peak_tmp[1];

        m_active_before = m_active;
    }

    // -------------------------------------------------------------------
    // Cleanup

    void delete_buffers()
    {
        qDebug("LadspaPlugin::delete_buffers() - start");

        if (param.count > 0)
            delete[] param_buffers;

        param_buffers = nullptr;

        qDebug("LadspaPlugin::delete_buffers() - end");
    }

    // -------------------------------------------------------------------

    bool init(const char* filename, const char* label, const LADSPA_RDF_Descriptor* rdf_descriptor_)
    {
        if (lib_open(filename))
        {
            LADSPA_Descriptor_Function descfn = (LADSPA_Descriptor_Function)lib_symbol("ladspa_descriptor");

            if (descfn)
            {
                unsigned long i = 0;
                while ((descriptor = descfn(i++)))
                {
                    if (strcmp(descriptor->Label, label) == 0)
                        break;
                }

                if (descriptor)
                {
                    handle = descriptor->instantiate(descriptor, get_sample_rate());

                    if (handle)
                    {
                        m_filename = strdup(filename);

                        if (is_ladspa_rdf_descriptor_valid(rdf_descriptor_, descriptor))
                            rdf_descriptor = ladspa_rdf_dup(rdf_descriptor_);

                        if (rdf_descriptor && rdf_descriptor->Title)
                            m_name = get_unique_name(rdf_descriptor->Title);
                        else
                            m_name = get_unique_name(descriptor->Name);

                        x_client = new CarlaEngineClient(this);

                        if (x_client->isOk())
                        {
                            return true;
                        }
                        else
                            set_last_error("Failed to register plugin client");
                    }
                    else
                        set_last_error("Plugin failed to initialize");
                }
                else
                    set_last_error("Could not find the requested plugin Label in the plugin library");
            }
            else
                set_last_error("Could not find the LASDPA Descriptor in the plugin library");
        }
        else
            set_last_error(lib_error());

        return false;
    }

private:
    LADSPA_Handle handle;
    const LADSPA_Descriptor* descriptor;
    const LADSPA_RDF_Descriptor* rdf_descriptor;

    float* param_buffers;
};

short add_plugin_ladspa(const char* filename, const char* label, const void* extra_stuff)
{
    qDebug("add_plugin_ladspa(%s, %s, %p)", filename, label, extra_stuff);

    short id = get_new_plugin_id();

    if (id >= 0)
    {
        LadspaPlugin* plugin = new LadspaPlugin(id);

        if (plugin->init(filename, label, (LADSPA_RDF_Descriptor*)extra_stuff))
        {
            plugin->reload();

            unique_names[id] = plugin->name();
            CarlaPlugins[id] = plugin;

            plugin->osc_register_new();
        }
        else
        {
            delete plugin;
            id = -1;
        }
    }
    else
        set_last_error("Maximum number of plugins reached");

    return id;
}

CARLA_BACKEND_END_NAMESPACE