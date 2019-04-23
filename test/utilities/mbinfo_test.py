#!/usr/bin/env python3

# Copyright 2019 Google Inc. All Rights Reserved.
#
# See README file for copying and redistribution conditions.

"""Tests for mbinfo command line app."""

import json
import os
import subprocess
import unittest

class MbinfoTest(unittest.TestCase):

  def setUp(self):
    self.cmd = '../../src/utilities/mbinfo'

  def testNoArgs(self):
    # Report a failure by calling exit(3).  There is no real failure.
    cmd = [self.cmd]
    raised = False
    try:
      subprocess.check_output(cmd)
    except subprocess.CalledProcessError as e:
      raised = True
      self.assertEqual(3, e.returncode)
      self.assertIn(b'initialization failed', e.output)
    self.assertTrue(raised)

  def testHelp(self):
    cmd = [self.cmd, '-h']
    output = subprocess.check_output(cmd)
    self.assertIn(b'basic statistics', output)

  def testDefaultOutputStyle(self):
    cmd = [self.cmd, '-Itestdata/mb21/TN136HS.309.snipped.mb21']
    output = subprocess.check_output(cmd)
    self.assertIn(b'MBIO Data Format ID:  21', output)

  def testJsonOutputStyle(self):
    cmd = [self.cmd, '-X1', '-Itestdata/mb21/TN136HS.309.snipped.mb21']
    output = subprocess.check_output(cmd)
    print (output)
    self.assertIn(b'"limits"', output)

    summary = json.loads(output)

    # Some entries currently have leading whitespace.
    bathy = summary['bathymetry_data']
    self.assertEqual('2.54', bathy['percent_zero_beams'].lstrip())
    self.assertEqual('0.00', bathy['percent_flagged_beams'].lstrip())

    limits = summary['limits']
    self.assertEqual('-124.505797065', limits['minimum_longitude'])

    expected = {
        'file_info': {
            'swath_data_file': 'TN136HS.309.snipped.mb21',
            'mbio_data_format_id': '21',
            'format_name': 'MBF_HSATLRAW',
            'informal_description': 'Raw Hydrosweep',
            'attributes': ('Hydrosweep DS, bathymetry and amplitude, 59 beams,;'
                           '                      ascii, Atlas Electronik.')
        },
        'data_totals': {
            'number_of_records': '2'
        },
        'bathymetry_data': {
            'max_beams_per_ping': '59',
            'number_beams': '118',
            'number_good_beams': '115',
            'percent_good_beams': '97.46',
            'number_zero_beams': '3',
            'percent_zero_beams': ' 2.54',
            'number_flagged_beams': '0',
            'percent_flagged_beams': ' 0.00'
        },
        'amplitude_data': {
            'max_beams_per_ping': '59',
            'number_beams': '118',
            'number_good_beams': '115',
            'percent_good_beams': ' 97.46',
            'number_zero_beams': '3',
            'percent_zero_beams': ' 2.54',
            'number_flagged_beams': '0',
            'percent_flagged_beams': ' 0.00'
        },
        'sidescan_data': {
            'max_pixels_per_ping': '0',
            'number_of_pixels': '0',
            'number_good_pixels': '0',
            'percent_good_pixels': ' 0.00',
            'number_zero_pixels': '0',
            'percent_zero_pixels': ' 0.00',
            'number_flagged_pixels': '0',
            'percent_flagged_pixels': ' 0.00'
        },
        'navigation_totals': {
            'total_time_hours': '0.0014',
            'total_track_length_km': '0.0036',
            'average_speed_km_per_hr': '2.6044',
            'average_speed_knots': '1.4078'
        },
        'start_of_data': {
            'time': '11 05 2001 00:01:44.000000  JD309',
            'time_iso': '2001-11-05T00:01:44.000000',
            'longitude': '-124.501602100',
            'latitude': '40.838493300',
            'depth_meters': '462.6000',
            'speed_km_per_hour': '1.4400',
            'speed_knots': '0.7784',
            'heading_degrees': '324.5000',
            'sonar_depth_meters': '0.0000',
            'sonar_altitude_meters': '462.6000'
        },
        'end_of_data': {
            'time': '11 05 2001 00:01:49.000000  JD309',
            'time_iso': '2001-11-05T00:01:49.000000',
            'longitude': '-124.501632600',
            'latitude': '40.838516200',
            'depth_meters': '462.4000',
            'speed_km_per_hour': '0.3600',
            'speed_knots': '0.1946',
            'heading_degrees': '324.9000',
            'sonar_depth_meters': '5.6000',
            'sonar_altitude_meters': '456.7000'
        },
        'limits': {
            'minimum_longitude': '-124.505797065',
            'maximum_longitude': '-124.497389636',
            'minimum_latitude': '40.836260446',
            'maximum_latitude': '40.840775308',
            'minimum_sonar_depth': '5.6000',
            'maximum_sonar_depth': '5.6000',
            'minimum_altitude': '456.7000',
            'maximum_altitude': '462.6000',
            'minimum_depth': '448.2000',
            'maximum_depth': '468.3000',
            'minimum_amplitude': '0.0000',
            'maximum_amplitude': '0.0000'
        }}
    self.assertEqual(expected, summary)

if __name__ == '__main__':
  unittest.main()
