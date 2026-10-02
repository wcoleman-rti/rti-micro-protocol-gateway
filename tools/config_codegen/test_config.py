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
from config_codegen import compile_config, ConfigError, emit

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
GATEWAY = f"""<gateway dds="dds.xml" route-budget="2" sample-budget="4">
<binding id="b" symbol="binding" type="Test" schema="test" fingerprint="{FINGERPRINT}"/>
<connection id="c" participant="P::p">
<stream name="r" endpoint="sub::r" binding="b" role="reader" capacity="4"/>
<stream name="w" endpoint="pub::w" binding="b" role="writer" capacity="4"/>
</connection><route id="route" input="c::r" output="c::w"/></gateway>"""


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
        self.assertIn('PGW_CompiledNativeStreamSeq pgw_config_native_streams', text)
        self.assertNotIn('pgw_config_route_count', text)
        self.assertNotIn('pgw_config_native_stream_count', text)
        self.assertNotIn('create_datawriter', text)
        self.assertNotIn('remote_writer_allocation', text)

    def test_unknown_elements_attributes_and_order(self):
        for xml in (GATEWAY.replace("<binding ", "<binding bogus='1' "),
                    GATEWAY.replace("</gateway>", "<unknown/></gateway>"),
                    GATEWAY.replace("<connection ", "<bogus ").replace("</connection>", "</bogus>")):
            self.reject(xml)

    def test_duplicate_missing_wrong_role(self):
        for xml in (GATEWAY.replace('name="w"', 'name="r"'),
                    GATEWAY.replace('binding="b"', 'binding="unknown"'),
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


if __name__ == "__main__":
    unittest.main()
