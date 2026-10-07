#
# (c) 2026 Copyright, Real-Time Innovations, Inc. All rights reserved.
#
# RTI grants Licensee a license to use, modify, compile, and create derivative
# works of the Software. Licensee has the right to distribute object form only
# for use with RTI products. The Software is provided "as is", with no warranty
# of any type, including any warranty for fitness for any purpose. RTI is under no
# obligation to maintain or support the Software. RTI shall not be liable for any
# incidental or consequential damages arising out of the use or inability to use
# the software.
#

import json
import os
from pathlib import Path
import shutil
import unittest
from config_codegen import compile_config, ConfigError, emit, parse_adapter_manifest
from adapter_codegen import generate as generate_adapter_idl
from control_codegen import generate as generate_control_idl
from dds_type_binding_codegen import (
    BindingGenerationError,
    generate as generate_dds_type_bindings,
)
from combine_idl import FlattenError, flatten

FINGERPRINT = "a" * 64
DDS = """<dds><qos_library name="Q"><qos_profile name="Bounded">
<datawriter_qos><history><kind>KEEP_LAST_HISTORY_QOS</kind><depth>1</depth></history>
<resource_limits><max_instances>4</max_instances><max_samples>4</max_samples><max_samples_per_instance>1</max_samples_per_instance></resource_limits>
<reliability><max_blocking_time><sec>0</sec><nanosec>0</nanosec></max_blocking_time></reliability></datawriter_qos>
<datareader_qos><history><kind>KEEP_LAST_HISTORY_QOS</kind><depth>1</depth></history>
<resource_limits><max_instances>4</max_instances><max_samples>4</max_samples><max_samples_per_instance>1</max_samples_per_instance></resource_limits>
</datareader_qos></qos_profile></qos_library>
<domain_library name="D"><domain name="d" domain_id="173">
<register_type name="T" type_ref="Test"/><topic name="topic" register_type_ref="T"/>
</domain></domain_library><domain_participant_library name="P">
<domain_participant name="p" domain_ref="D::d">
<publisher name="pub"><data_writer name="w" topic_ref="topic"><datawriter_qos base_name="Q::Bounded"/></data_writer></publisher>
<subscriber name="sub"><data_reader name="r" topic_ref="topic"><datareader_qos base_name="Q::Bounded"/></data_reader></subscriber>
</domain_participant></domain_participant_library></dds>"""
GATEWAY = f"""<gateway dds="dds.xml" sample-budget="4">
<type-binding id="b" symbol="binding" type="Test" schema="test" fingerprint="{FINGERPRINT}"/>
<connection id="c" participant="P::p" adapter="connext_micro">
<stream name="r" endpoint="sub::r" type-binding="b" role="reader" capacity="4"/>
<stream name="w" endpoint="pub::w" type-binding="b" role="writer" capacity="4"/>
</connection><session name="s"><route id="route" input="c::r" output="c::w"/></session></gateway>"""


class ConfigurationRequirements(unittest.TestCase):
    def setUp(self):
        self.directory = Path.cwd() / f"config-test-{os.getpid()}"
        self.directory.mkdir()
        (self.directory / "dds.xml").write_text(DDS)
        self.gateway = self.directory / "gateway.xml"
        self.gateway.write_text(GATEWAY)

    def tearDown(self):
        shutil.rmtree(self.directory)

    def reject(self, xml):
        self.gateway.write_text(xml)
        with self.assertRaises(ConfigError):
            compile_config(self.gateway)

    def test_valid_references_emit_only_adoption(self):
        compiled = compile_config(self.gateway)
        output = self.directory / "configuration.c"
        emit(compiled, output)
        text = output.read_text()
        self.assertIn('PGW_DDSConfig pgw_config_c', text)
        self.assertIn('REDA_DEFINE_SEQUENCE_INITIALIZER_W_LOAN(c_endpoints, 2, 2', text)
        self.assertIn('REDA_DEFINE_SEQUENCE_INITIALIZER_W_LOAN(pgw_routes, 1, 1', text)
        self.assertIn('REDA_DEFINE_SEQUENCE_INITIALIZER_W_LOAN(pgw_sessions, 1, 1', text)
        self.assertIn('const char *const pgw_config_control_session = NULL;', text)
        self.assertIn('PGW_CompiledAdapterStreamSeq pgw_config_adapter_streams', text)
        self.assertNotIn('pgw_config_route_count', text)
        self.assertNotIn('pgw_config_native_stream_count', text)
        self.assertNotIn('create_datawriter', text)
        self.assertNotIn('remote_writer_allocation', text)

    def test_adapter_connection_uses_common_connection_element(self):
        xml = GATEWAY.replace(
            '<connection id="c" participant="P::p" adapter="connext_micro">',
            '<connection id="c" adapter="can" receive-budget="4" write-capacity="4">')
        xml = xml.replace('endpoint="sub::r"', 'endpoint="reader"')
        xml = xml.replace('endpoint="pub::w"', 'endpoint="writer"')
        self.gateway.write_text(xml)
        compiled = compile_config(self.gateway)
        self.assertEqual(compiled[1], [])
        self.assertEqual(compiled[4]["c_receive_budget"], 4)
        self.assertEqual(compiled[4]["c_write_capacity"], 4)
        self.assertEqual(len(compiled[5]), 2)
        output = self.directory / "configuration.c"
        emit(compiled, output)
        self.assertIn("PGW_CompiledAdapterStreamSeq pgw_config_adapter_streams",
                      output.read_text())

    def test_route_membership_is_explicit_and_consumers_are_unique(self):
        self.reject(GATEWAY.replace(' name="s"', ''))
        self.reject(GATEWAY.replace(
            "</session></gateway>",
            '</session><session name="s2"><route id="route2" '
            'input="c::r" output="c::w"/></session></gateway>'))
        self.reject(GATEWAY.replace(
            "</gateway>",
            '<control session="missing"><resource kind="route" ref="route" '
            'actions="pause resume"/></control></gateway>'))
        self.gateway.write_text(GATEWAY)
        compiled = compile_config(self.gateway)
        self.assertEqual(compiled[2], [{
            "name": "s",
            "routes": [{
                "id": "route", "input": "c::r", "output": "c::w",
                "session": "s"
            }]
        }])

    def test_unknown_elements_attributes_and_order(self):
        for xml in (GATEWAY.replace("<type-binding ", "<type-binding bogus='1' "),
                    GATEWAY.replace("</gateway>", "<unknown/></gateway>"),
                    GATEWAY.replace("<connection ", "<bogus ").replace("</connection>", "</bogus>")):
            self.reject(xml)

    def test_binding_conversion_declarations_are_strict(self):
        attributes = (
            'schema-version="1" representation-name="test.native" '
            'native-type="TestValue" native-header="test.h" '
            'support-header="testSupport.h" conversion="callbacks" '
            'dds-to-native="test_from_dds" native-to-dds="test_to_dds" '
            'supports-timestamp="false"')
        valid = GATEWAY.replace(
            f'fingerprint="{FINGERPRINT}"/>',
            f'fingerprint="{FINGERPRINT}" {attributes}/>')
        self.gateway.write_text(valid)
        compile_config(self.gateway)
        views = valid.replace(
            'supports-timestamp="false"',
            'supports-timestamp="false" bind-view="test_bind_view" '
            'write-view="test_write_view"')
        self.gateway.write_text(views)
        compile_config(self.gateway)

        invalid = (
            valid.replace(' native-to-dds="test_to_dds"', ''),
            valid.replace('conversion="callbacks"', 'conversion="unknown"'),
            valid.replace('supports-timestamp="false"', 'supports-timestamp="maybe"'),
            valid.replace('conversion="callbacks"', 'conversion="fieldwise"'),
            views.replace('bind-view="test_bind_view" ', ''),
            views.replace('write-view="test_write_view"', ''),
        )
        for xml in invalid:
            with self.subTest(xml=xml), self.assertRaises(ConfigError):
                self.gateway.write_text(xml)
                compile_config(self.gateway)

    def test_duplicate_missing_wrong_role(self):
        for xml in (GATEWAY.replace('name="w"', 'name="r"'),
                    GATEWAY.replace('type-binding="b"', 'type-binding="unknown"'),
                    GATEWAY.replace('participant="P::p"', 'participant="P::missing"'),
                    GATEWAY.replace('endpoint="sub::r"', 'endpoint="pub::w"'),
                    GATEWAY.replace('output="c::w"', 'output="c::r"')):
            self.reject(xml)

    def test_unbounded_and_conflicting_metadata(self):
        for xml in (GATEWAY.replace('capacity="4"', 'capacity="0"'),
                    GATEWAY.replace('capacity="4"', 'capacity="65536"'),
                    GATEWAY.replace('role="reader"', 'role="reader" preserve-source-timestamp="true"'),
                    GATEWAY.replace('role="writer"', 'role="writer" preserve-source-timestamp="maybe"')):
            self.reject(xml)

    def test_type_fingerprint_and_entity_resolution(self):
        self.reject(GATEWAY.replace('type="Test"', 'type="Other"'))
        inventory = self.directory / "manifest.json"
        inventory.write_text(json.dumps({"schema": "test", "fingerprint": "b" * 64}))
        self.gateway.write_text(GATEWAY)
        with self.assertRaises(ConfigError):
            compile_config(self.gateway, inventory=inventory)

    def test_dtd_forbidden(self):
        self.reject('<!DOCTYPE gateway [<!ENTITY x "test">]>' + GATEWAY)

    def test_unsafe_qos_and_unresolved_inheritance(self):
        for dds in (DDS.replace("<max_instances>4", "<max_instances>-1"),
                    DDS.replace("<depth>1", "<depth>2"),
                    DDS.replace("<sec>0", "<sec>1"),
                    DDS.replace('base_name="Q::Bounded"', 'base_name="Q::Missing"')):
            (self.directory / "dds.xml").write_text(dds)
            with self.assertRaises(ConfigError):
                compile_config(self.gateway)

    def test_reader_loan_margin_is_bounded_and_not_writer_retention(self):
        reader_start = DDS.index("<datareader_qos>")
        writer, reader = DDS[:reader_start], DDS[reader_start:]
        reader = reader.replace("<max_samples>4", "<max_samples>8").replace(
            "<max_samples_per_instance>1", "<max_samples_per_instance>2")
        (self.directory / "dds.xml").write_text(writer + reader)
        compile_config(self.gateway)
        for dds in (writer + reader.replace("<max_samples>8", "<max_samples>4"),
                    writer + reader.replace("<max_samples_per_instance>2",
                                             "<max_samples_per_instance>3"),
                    writer.replace("<max_samples_per_instance>1",
                                   "<max_samples_per_instance>2") + reader):
            (self.directory / "dds.xml").write_text(dds)
            with self.assertRaises(ConfigError):
                compile_config(self.gateway)

    def test_duplicate_dds_inventory_rejected(self):
        for dds in (DDS.replace('<topic name="topic" register_type_ref="T"/>',
                               '<topic name="topic" register_type_ref="T"/>' * 2),
                    DDS.replace('<register_type name="T" type_ref="Test"/>',
                               '<register_type name="T" type_ref="Test"/>' * 2),
                    DDS.replace('<publisher name="pub">',
                               '<publisher name="pub"/><publisher name="pub">')):
            (self.directory / "dds.xml").write_text(dds)
            with self.assertRaises(ConfigError):
                compile_config(self.gateway)

    def test_control_requires_compile_time_opt_in_and_selects_routes(self):
        xml = GATEWAY.replace(
            "</gateway>",
            '<control session="s"><resource kind="route" ref="route" actions="pause resume"/></control></gateway>')
        self.gateway.write_text(xml)
        with self.assertRaises(ConfigError):
            compile_config(self.gateway)
        compiled = compile_config(self.gateway, remote_control=True)
        self.assertEqual(compiled[6], [{
            "kind": "route",
            "ref": "route",
            "adapter": "core",
            "enum": "RESOURCE_ROUTE_ROUTE",
            "command_capabilities": (1 << 6) | (1 << 7),
            "telemetry_capabilities": 0,
        }])
        output = self.directory / "control.c"
        emit(compiled, output)
        self.assertIn("pgw_control_resource_count", output.read_text())
        self.assertIn('pgw_config_control_session = "s";', output.read_text())

    def test_control_rejects_unsupported_or_unresolved_resources(self):
        for control in (
            '<control session="s"><resource kind="connection" ref="c" actions="up down"/></control>',
            '<control session="s"><resource kind="route" ref="missing" actions="pause resume"/></control>',
            '<control session="s"><resource kind="route" ref="route" actions="pause resume"/></control>'
            '<control session="s"><resource kind="route" ref="route" actions="pause resume"/></control>',
        ):
            self.gateway.write_text(GATEWAY.replace("</gateway>", control + "</gateway>"))
            with self.assertRaises(ConfigError):
                compile_config(self.gateway, remote_control=True)

    def test_control_idl_is_deterministic_and_correlated(self):
        gateway = self.directory / "controlled.xml"
        gateway.write_text(GATEWAY.replace(
            "</gateway>",
            '<control session="s"><resource kind="route" ref="route" actions="pause resume"/></control></gateway>'))
        common = (Path(__file__).resolve().parents[2] / "core" / "control" / "idl" /
                  "control_common.idl")
        output = self.directory / "controller.idl"
        generate_control_idl(gateway, common, output)
        first = output.read_text()
        generate_control_idl(gateway, common, output)
        self.assertEqual(output.read_text(), first)
        self.assertIn("RESOURCE_ROUTE_ROUTE", first)
        self.assertIn("octet publication_handle[16]", first)
        self.assertIn("long publication_sequence_high", first)
        self.assertIn("unsigned long publication_sequence_low", first)
        self.assertNotIn("command_id", first)

    def test_linked_adapter_manifests_gate_selected_actions(self):
        manifest = self.directory / "connext_micro.xml"
        manifest.write_text(
            '<adapter name="connext_micro" version="1" control-api-version="1">'
            '<control>'
            '<capability kind="connection" actions="up|down"/>'
            '<capability kind="input" actions="enable|disable"/>'
            '<capability kind="output" actions="enable|disable"/>'
            '</control>'
            '</adapter>')
        controlled = GATEWAY.replace(
            "</gateway>",
            '<control session="s">'
            '<resource kind="connection" ref="c" actions="up"/>'
            '<resource kind="input" ref="c::r" actions="enable disable"/>'
            '<resource kind="output" ref="c::w" actions="disable"/>'
            '</control></gateway>')
        self.gateway.write_text(controlled)
        with self.assertRaises(ConfigError):
            compile_config(self.gateway, remote_control=True)
        compiled = compile_config(
            self.gateway, remote_control=True, adapter_manifests=(manifest,))
        resources = compiled[6]
        self.assertEqual([resource["kind"] for resource in resources],
                         ["connection", "input", "output"])
        self.assertEqual(resources[0]["command_capabilities"], 1)
        self.assertEqual(resources[0]["adapter"], "connext_micro")
        output = self.directory / "adapter.idl"
        header = self.directory / "control_manifest.h"
        common = (Path(__file__).resolve().parents[2] / "core" / "control" / "idl" /
                  "control_common.idl")
        generate_adapter_idl(manifest, common, output, header)
        self.assertIn("module PGW_Adapter_connext_micro", output.read_text())
        self.assertIn("ACTION_MASK = 63", output.read_text())
        self.assertIn("PGW_CONNEXT_MICRO_CONTROL_ACTION_MASK UINT32_C(63)",
                      header.read_text())

    def test_adapter_manifest_sections_and_pipe_delimited_actions(self):
        manifest = self.directory / "manifest.xml"
        valid = (
            '<adapter name="adapter" version="1" control-api-version="1">'
            '<control><capability kind="connection" actions="up|down"/></control>'
            '<telemetry><metric resource-kind="connection" name="frames" '
            'scalar="uint64" unit="frames"/></telemetry></adapter>')
        manifest.write_text(valid)
        name, metadata = parse_adapter_manifest(manifest)
        self.assertEqual(name, "adapter")
        self.assertEqual(metadata["capabilities"]["connection"]["actions"], ["up", "down"])
        self.assertIn(("connection", "frames"), metadata["metrics"])

        invalid = (
            valid.replace('<control>', '').replace('</control>', ''),
            valid.replace('<telemetry>', '').replace('</telemetry>', ''),
            valid.replace('actions="up|down"', 'actions="up down"'),
            valid.replace('<control>', '<control><unknown/>'),
            valid.replace('actions="up|down"', 'actions="up|unsupported"'),
            valid.replace(
                '</control>',
                '<capability kind="connection" actions="up"/></control>'),
        )
        for xml in invalid:
            manifest.write_text(xml)
            with self.subTest(xml=xml), self.assertRaises(ConfigError):
                parse_adapter_manifest(manifest)

    def test_dds_type_bindings_are_generated_from_rti_type_xml(self):
        types_xml = self.directory / "types.xml"
        types_xml.write_text(
            '<dds><types><module name="Example"><struct name="Sample">'
            '<member name="key" type="uint32" key="true"/>'
            '<member name="value" type="int32"/>'
            '</struct></module></types></dds>')
        gateway_xml = self.directory / "bindings.xml"
        common = (
            'schema="example.sample" schema-version="1" fingerprint="'
            + FINGERPRINT + '" representation-name="example.sample.native" '
            'native-type="ExampleSample" native-header="example_sample.h" '
            'support-header="exampleSupport.h" conversion="callbacks" '
            'dds-to-native="from_example_sample" native-to-dds="to_example_sample" '
            'sample-copy="copy_example_sample" direct-native-write="true" '
            'validate-native="validate_example_sample" '
            'bind-view="bind_example_sample_view" '
            'write-view="write_example_sample_view"')
        gateway_xml.write_text(
            '<gateway><type-binding id="reader" symbol="reader_binding" '
            'type="Example::Sample" ' + common +
            ' supports-timestamp="true" register-keys="register_reader_keys"/>'
            '<type-binding id="writer" symbol="writer_binding" '
            'type="Example::Sample" ' + common +
            ' register-keys="register_writer_keys"/></gateway>')
        output = self.directory / "generated_bindings.c"
        generate_dds_type_bindings(types_xml, gateway_xml, output)
        first = output.read_text()
        generate_dds_type_bindings(types_xml, gateway_xml, output)
        self.assertEqual(output.read_text(), first)
        self.assertIn("Example_SampleDataReader_take", first)
        self.assertIn("Example_SampleDataReader_return_loan", first)
        self.assertIn("Example_SampleDataWriter_write_w_timestamp", first)
        self.assertIn("static const PGW_SampleRepresentation pgw_representation_0", first)
        self.assertIn("copy_example_sample(sample, out, size)", first)
        self.assertIn("&pgw_access_0", first)
        self.assertIn("Example_SampleTypePlugin_get", first)
        self.assertIn("direct_write_safe = true", first)
        self.assertIn("validate_native = validate_example_sample", first)
        self.assertIn("Example_SampleDataWriter_write", first)
        self.assertIn("Example_SampleDataWriter_write_w_timestamp", first)
        self.assertIn("write_native = pgw_binding_reader_write_native", first)
        self.assertIn("bind_view = bind_example_sample_view", first)
        self.assertIn("write_view = write_example_sample_view", first)
        self.assertEqual(first.count("static const PGW_SampleRepresentation "), 1)
        self.assertIn("const PGW_DDSTypeBinding reader_binding", first)
        self.assertIn("const PGW_DDSTypeBinding writer_binding", first)

        gateway_xml.write_text(
            '<gateway><type-binding id="unknown" symbol="unknown_binding" '
            'type="Example::Missing" ' + common + '/></gateway>')
        with self.assertRaises(BindingGenerationError):
            generate_dds_type_bindings(types_xml, gateway_xml, output)
        gateway_xml.write_text(
            '<gateway><type-binding id="unpaired" symbol="unpaired_binding" '
            'type="Example::Sample" ' +
            common.replace('bind-view="bind_example_sample_view" ', '') +
            '/></gateway>')
        with self.assertRaises(BindingGenerationError):
            generate_dds_type_bindings(types_xml, gateway_xml, output)

    def test_fieldwise_dds_type_binding_conversion_is_generated_from_xml(self):
        types_xml = self.directory / "fieldwise-types.xml"
        types_xml.write_text(
            '<dds><types><module name="Example"><struct name="Probe">'
            '<member name="id" type="uint32" key="true"/>'
            '<member name="reading" type="int32"/>'
            '</struct></module></types></dds>')
        gateway_xml = self.directory / "fieldwise-bindings.xml"
        gateway_xml.write_text(
            '<gateway><type-binding id="probe" symbol="probe_binding" '
            'type="Example::Probe" schema="example.probe" schema-version="1" '
            f'fingerprint="{FINGERPRINT}" representation-name="example.probe.native" '
            'native-type="ExampleProbe" native-header="example_probe.h" '
            'support-header="probeSupport.h" conversion="fieldwise" '
            'register-key-value="1" supports-timestamp="true" '
            'direct-native-write="true" '
            'bind-view="PGW_probe_bind_view" '
            'write-view="PGW_probe_write_view"/></gateway>')
        output = self.directory / "fieldwise_bindings.c"
        generate_dds_type_bindings(types_xml, gateway_xml, output)
        text = output.read_text()
        header = (self.directory / "dds_type_bindings.h").read_text()
        self.assertIn("ExampleProbe *value = out", text)
        self.assertIn("value->id = wire->id", text)
        self.assertIn("value->reading = wire->reading", text)
        self.assertIn("wire->id = value->id", text)
        self.assertIn("wire->reading = value->reading", text)
        self.assertIn("fieldwise native member type mismatch", text)
        self.assertIn("typedef struct ExampleProbe", header)
        self.assertIn("uint32_t id;", header)
        self.assertIn("int32_t reading;", header)
        self.assertIn("scratch.id = 1u", text)
        self.assertIn("Example_ProbeDataWriter_register_instance_w_timestamp", text)
        self.assertIn("direct_write_safe = true", text)
        self.assertIn("write_view = PGW_probe_write_view", text)
        self.assertIn("bind_view = PGW_probe_bind_view", text)
        types_xml.write_text(
            '<dds><types><module name="Example"><struct name="Probe">'
            '<member name="id" type="uint32" key="true"/>'
            '<member name="reading" type="int32" arrayDimensions="2"/>'
            '</struct></module></types></dds>')
        with self.assertRaises(BindingGenerationError):
            generate_dds_type_bindings(types_xml, gateway_xml, output)

    def test_control_name_normalization_collision_is_rejected(self):
        xml = GATEWAY.replace(
            "</session></gateway>",
            '<route id="Route" input="c::r" output="c::w"/></session>'
            '<control session="s">'
            '<resource kind="route" ref="route" actions="pause resume"/>'
            '<resource kind="route" ref="Route" actions="pause resume"/>'
            '</control></gateway>')
        self.gateway.write_text(xml)
        with self.assertRaises(ConfigError):
            compile_config(self.gateway, remote_control=True)

    def test_selected_metric_is_bounded_and_typed_from_adapter_manifest(self):
        manifest = self.directory / "can.xml"
        manifest.write_text(
            '<adapter name="can" version="1" control-api-version="1">'
            '<control><capability kind="connection" actions="up|down"/></control>'
            '<telemetry><metric resource-kind="connection" name="received_frames" '
            'scalar="uint64" unit="frames"/></telemetry>'
            '</adapter>')
        xml = GATEWAY.replace('adapter="connext_micro"', 'adapter="can"')
        xml = xml.replace(
            "</gateway>",
            '<control session="s" minimum-telemetry-period-ms="100">'
            '<resource kind="connection" ref="c" actions="up down"/>'
            '<metric resource-kind="connection" resource="c" name="received_frames"/>'
            '</control></gateway>')
        self.gateway.write_text(xml)
        with self.assertRaises(ConfigError):
            compile_config(self.gateway, remote_control=True)
        compiled = compile_config(
            self.gateway, remote_control=True, adapter_manifests=(manifest,))
        resource = compiled[6][0]
        metric = compiled[7][0]
        self.assertEqual(resource["telemetry_capabilities"], 1)
        self.assertEqual(metric["id"], 0)
        self.assertEqual(metric["scalar"], "uint64")
        self.assertEqual(metric["unit"], "frames")
        self.assertEqual(compiled[8], 100)
        output = self.directory / "controller.idl"
        common = (Path(__file__).resolve().parents[2] / "core" / "control" / "idl" /
                  "control_common.idl")
        generate_control_idl(
            self.gateway, common, output, adapter_manifests=(manifest,))
        idl = output.read_text()
        self.assertIn("enum TelemetryKind", idl)
        self.assertIn("unsigned long long uint64_value", idl)
        self.assertIn("TelemetryKind kind; //@key", idl)

    def test_metric_rejects_missing_ceiling_or_resource_selection(self):
        manifest = self.directory / "can.xml"
        manifest.write_text(
            '<adapter name="can" version="1" control-api-version="1">'
            '<telemetry><metric resource-kind="connection" name="received_frames" '
            'scalar="uint64" unit="frames"/></telemetry>'
            '</adapter>')
        cases = (
            '<control session="s"><resource kind="connection" ref="c" actions="up"/>'
            '<metric resource-kind="connection" resource="c" name="received_frames"/>'
            '</control>',
            '<control session="s" minimum-telemetry-period-ms="100">'
            '<metric resource-kind="connection" resource="c" name="received_frames"/>'
            '</control>',
        )
        for control in cases:
            xml = GATEWAY.replace('adapter="connext_micro"', 'adapter="can"')
            self.gateway.write_text(xml.replace("</gateway>", control + "</gateway>"))
            with self.assertRaises(ConfigError):
                compile_config(
                    self.gateway, remote_control=True, adapter_manifests=(manifest,))

    def test_selected_telemetry_metadata_generates_kind_and_writer_data(self):
        manifest = self.directory / "can.xml"
        manifest.write_text(
            '<adapter name="can" version="1" control-api-version="1">'
            '<control><capability kind="connection" actions="up|down"/></control>'
            '<telemetry><metric resource-kind="connection" name="received_frames" '
            'scalar="uint64" unit="frames"/></telemetry>'
            '</adapter>')
        controlled = GATEWAY.replace('adapter="connext_micro"', 'adapter="can"')
        controlled = controlled.replace(
            "</gateway>",
            '<control session="s" minimum-telemetry-period-ms="100">'
            '<resource kind="connection" ref="c" actions="up down"/>'
            '<metric resource-kind="connection" resource="c" name="received_frames"/>'
            '</control></gateway>')
        self.gateway.write_text(controlled)
        config = compile_config(
            self.gateway, remote_control=True, adapter_manifests=(manifest,))
        self.assertEqual(config[6][0]["telemetry_capabilities"], 1)
        self.assertEqual(config[7][0]["scalar"], "uint64")
        self.assertEqual(config[8], 100)
        output = self.directory / "controller-telemetry.idl"
        common = (Path(__file__).resolve().parents[2] / "core" / "control" / "idl" /
                  "control_common.idl")
        generate_control_idl(
            self.gateway, common, output, adapter_manifests=(manifest,))
        idl = output.read_text()
        self.assertIn("TELEMETRY_RECEIVED_FRAMES", idl)
        self.assertIn("union TelemetryValue switch", idl)
        self.assertIn("unsigned long long uint64_value", idl)

    def test_dependency_aware_idl_flattening(self):
        (self.directory / "common.idl").write_text("module Common { struct Value { long x; }; };\n")
        (self.directory / "adapter.idl").write_text(
            '#include "common.idl"\nmodule Adapter { struct Sample { Common::Value value; }; };\n')
        (self.directory / "service.idl").write_text(
            '#include "adapter.idl"\n#include "common.idl"\nmodule Service {};\n')
        flattened = flatten([self.directory / "service.idl"])
        self.assertLess(flattened.index("module Common"), flattened.index("module Adapter"))
        self.assertLess(flattened.index("module Adapter"), flattened.index("module Service"))
        self.assertEqual(flattened.count("module Common"), 1)
        (self.directory / "cycle-a.idl").write_text('#include "cycle-b.idl"\n')
        (self.directory / "cycle-b.idl").write_text('#include "cycle-a.idl"\n')
        with self.assertRaises(FlattenError):
            flatten([self.directory / "cycle-a.idl"])
        (self.directory / "missing.idl").write_text('#include "absent.idl"\n')
        with self.assertRaises(FlattenError):
            flatten([self.directory / "missing.idl"])


if __name__ == "__main__":
    unittest.main()
