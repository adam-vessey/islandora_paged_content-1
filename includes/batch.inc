<?php

/**
 * @file
 * This file contains all the batch operations for paged content.
 */

use Drupal\Component\Utility\Html;

/**
 * Sets up a batch operation.
 *
 * @param array $operands
 *   All the pages to operate on.
 * @param array $context
 *   The context of this batch operation.
 * @param array $message
 *   Values are used as parameters to t() and drupal_set_message() that will be
 *   displayed when the batch is finished. Placeholders include, but are not
 *   limited to: @successful, @failed, @total.
 *
 * @return mixed
 *   The current operand the operation will process.
 */
function islandora_paged_content_start_batch_operation(array $operands, array &$context, array $message = []) {
  if (!isset($context['sandbox']['progress'])) {
    $results =& $context['results'];
    if (empty($results)) {
      // "files" will be deleted upon failure or success of the batch.
      // "successful" an array of boolean values indicating operation success.
      // "messages" array of messages to display on finishing the batch
      // values are used as parameters to t() and drupal_set_message().
      $results = [
        'files' => [],
        'successful' => [],
        'messages' => [],
      ];
    }
    $context['sandbox']['progress'] = 0;
    $context['sandbox']['total'] = count($operands);
    $context['results']['messages'][] = $message;
  }
  $operand_index = $context['sandbox']['progress'];
  return isset($operands[$operand_index]) ? $operands[$operand_index] : FALSE;
}

/**
 * Ends the batch operation.
 *
 * @param array $context
 *   The context of this batch operation.
 * @param int $count
 *   The number of steps taken in the operation, typically 1.
 * @param array $message
 *   Values are used as parameters to t() and drupal_set_message() that will be
 *   displayed when the batch is finished.
 *
 * @see islandora_paged_contnet_start_batch_operation()
 *
 * @return bool
 *   TRUE if the batch operation is complete, FALSE otherwise.
 */
function islandora_paged_content_end_batch_operation(array &$context, $count = 1, array $message = []) {
  $context['sandbox']['progress'] += $count;
  if ($context['sandbox']['progress'] != $context['sandbox']['total']) {
    $context['finished'] = $context['sandbox']['progress'] / $context['sandbox']['total'];
    return FALSE;
  }
  $context['results']['messages'][] = $message;
  return TRUE;
}

/**
 * The batch 'finished' callback.
 */
function islandora_paged_content_batch_finished($success, $results, $operations) {
  if ($success) {
    // Delete any lingering files.
    if (isset($results['files'])) {
      foreach ($results['files'] as $file) {
        if (file_exists($file)) {
          file_unmanaged_delete($file);
        }
      }
    }
    // Build generic place holders for the message.
    if (isset($results['successful'])) {
      $total = count($results['successful']);
      $successful = count(array_filter($results['successful']));
      $failed = $total - $successful;
      $place_holders = [
        '@successful' => $successful,
        '@failed' => $failed,
        '@total' => $total,
      ];
      // Display all messages/errors/warnings.
      $messages = array_filter($results['messages']);
      foreach ($messages as $message) {
        $vars = isset($message[1]) ? array_merge($message[1], $place_holders) : $place_holders;
        $status = isset($message[2]) ? $message[2] : 'status';
        $message = format_string($message[0], $vars);
        drupal_set_message(Html::escape($message), $status);
      }
    }
  }
  else {
    // Generic Error Message.
    $error_operation = reset($operations);
    $message = t(
      'An error occurred while processing %error_operation with arguments: @arguments',
      [
        '%error_operation' => $error_operation[0],
        '@arguments' => print_r($error_operation[1], TRUE),
      ]
    );
    drupal_set_message($message, 'error');
  }
}

/**
 * Gets the batch definition for creating a PDF from the given pages.
 *
 * This also updates each page object with a corresponding pdf.
 *
 * @param AbstractObject $paged_content
 *   The paged_content the pages belong to.
 * @param array $pages
 *   All the pages to include in the PDF.
 * @param array $options
 *   Options to pass into convert.
 *
 * @return array
 *   The batch definition.
 */
function islandora_paged_content_create_pdf_batch(AbstractObject $paged_content, array $pages, array $options) {
  return [
    'operations' => [
      [
        'islandora_paged_content_create_pdf_page_batch_operation',
        [$pages, $options],
      ],
      ['islandora_paged_content_create_pdf_paged_content_batch_operation', [$paged_content->id]],
    ],
    'finished' => 'islandora_paged_content_batch_finished',
    'title' => t('Creating PDF from @label ...', ['@label' => $paged_content->label]),
    'init_message' => t('Preparing to create PDF from @label ...', ['@label' => $paged_content->label]),
    'progress_message' => t('Time elapsed: @elapsed <br/>Estimated time remaining @estimate.'),
    'error_message' => t('An error has occurred.'),
    'file' => drupal_get_path('module', 'islandora_paged_content') . '/includes/batch.inc',
  ];
}

/**
 * Batch operation for converting a page into a PDF.
 *
 * @param array $pages
 *   All the pages to convert.
 * @param array $options
 *   Options to pass into convert.
 * @param array $context
 *   The context of this batch operation.
 */
function islandora_paged_content_create_pdf_page_batch_operation(array $pages, array $options, array &$context) {
  module_load_include('inc', 'islandora_paged_content', 'includes/utilities');
  $page = islandora_paged_content_start_batch_operation($pages, $context);
  if ($page) {
    $page = islandora_object_load($page);
    $pdf_file = islandora_paged_content_page_derive_pdf($page, $options);
    $context['results']['files'][] = $pdf_file;
    $context['results']['pages'][] = $pdf_file;
    $context['results']['successful'][] = islandora_paged_content_update_datastream($page, $pdf_file, 'PDF');
    islandora_paged_content_end_batch_operation($context);
    $context['message'] = t(
      'Converting page @page of @total into a PDF ...',
      [
        '@page' => $context['sandbox']['progress'],
        '@total' => $context['sandbox']['total'],
      ]
    );
  }
}

/**
 * Batch operation for combining PDF files.
 *
 * @param string $paged_content_id
 *   The paged content identifier to store the PDF file.
 * @param array $context
 *   The context of this batch operation.
 */
function islandora_paged_content_create_pdf_paged_content_batch_operation($paged_content_id, array &$context) {
  module_load_include('inc', 'islandora_paged_content', 'includes/utilities');
  $paged_content = islandora_object_load($paged_content_id);
  if (empty($context['results']['pages'])) {
    return;
  }
  islandora_paged_content_start_batch_operation($context['results']['pages'], $context);
  $pdf_file = \Drupal::service("file_system")->realpath("temporary://{$paged_content->id}_PDF.pdf");
  // Combined # of pages at a time.
  $files = array_slice($context['results']['pages'], $context['sandbox']['progress'], 10);
  $append = $context['sandbox']['progress'] != 0;
  $context['results']['successful'][] = $append ? islandora_paged_content_pdf_append($pdf_file, $files) : islandora_paged_content_pdf_combine($files, $pdf_file);
  $finished_message = ['Created PDF with @pages pages.', ['@pages' => count($context['results']['pages'])]];
  if (islandora_paged_content_end_batch_operation($context, count($files), $finished_message)) {
    islandora_paged_content_update_datastream($paged_content, $pdf_file, 'PDF');
    $context['results']['files'][] = $pdf_file;
  }
  $context['message'] = t(
    'Adding page @page of @total to the PDF ...',
    [
      '@page' => $context['sandbox']['progress'],
      '@total' => $context['sandbox']['total'],
    ]
  );
}

/**
 * Gets the batch definition for creating OCR and HOCR.
 *
 * @param AbstractObject $paged_content
 *   The paged content the pages belong to.
 * @param array $pages
 *   All the pages to process.
 * @param array $options
 *   Options to pass into tesseract/gimp.
 *
 * @return array
 *   The batch definition.
 */
function islandora_paged_content_create_ocr_batch(AbstractObject $paged_content, array $pages, array $options) {
  $return = [
    'operations' => [
      [
        'islandora_paged_content_create_ocr_batch_operation',
        [$pages, $options],
      ],
    ],
    'finished' => 'islandora_paged_content_batch_finished',
    'title' => t('Performing OCR on @label ...', ['@label' => $paged_content->label]),
    'init_message' => t('Preparing to perform OCR on @label ...', ['@label' => $paged_content->label]),
    'progress_message' => t('Time elapsed: @elapsed <br/>Estimated time remaining @estimate.'),
    'error_message' => t('An error has occurred.'),
    'file' => drupal_get_path('module', 'islandora_paged_content') . '/includes/batch.inc',
  ];
  if ($options['aggregate_ocr']) {
    $return['operations'][] = ['islandora_paged_content_create_ocr_paged_content_batch_operation', [$paged_content->id]];
  }
  return $return;
}

/**
 * Batch operation for creating the OCR and HOCR.
 *
 * @param array $pages
 *   All the pages to perform OCR on.
 * @param array $options
 *   Options to pass into tesseract/gimp.
 * @param array $context
 *   The context of this batch operation.
 */
function islandora_paged_content_create_ocr_batch_operation(array $pages, array $options, array &$context) {
  module_load_include('inc', 'islandora_paged_content', 'includes/utilities');
  $page = islandora_paged_content_start_batch_operation($pages, $context);
  if ($page) {
    $page = islandora_object_load($page);
    $ocr = islandora_paged_content_page_generate_ocr_datastreams($page, $options, FALSE);
    foreach ($ocr as $dsid => $result) {
      if ($result !== FALSE) {
        $context['results']['files'][] = $result;
        // OCR files will be required for concatenation.
        if ($dsid == 'OCR') {
          $context['results']['pages'][] = $result;
        }
        $context['results']['successful'][] = islandora_paged_content_update_datastream($page, $result, $dsid);
      }
    }
    islandora_paged_content_end_batch_operation(
      $context,
      1,
      [
        'Derived @derived of @set_size OCR/HOCR datastreams.',
        [
          '@set_size' => count($context['results']['successful']),
          '@derived' => count(array_filter($context['results']['successful'])),
        ],
      ]
    );
    $context['message'] = t(
      'Performing OCR on page @page of @total ...',
      [
        '@page' => $context['sandbox']['progress'],
        '@total' => $context['sandbox']['total'],
      ]
    );
  }
}

/**
 * Batch operation for consolidating OCR.
 *
 * @param string $paged_content_id
 *   The paged content identifier to store the OCR.
 * @param array $context
 *   The context of this batch operation.
 */
function islandora_paged_content_create_ocr_paged_content_batch_operation($paged_content_id, array &$context) {
  module_load_include('inc', 'islandora_paged_content', 'includes/utilities');
  $paged_content = islandora_object_load($paged_content_id);
  if (empty($context['results']['pages'])) {
    return;
  }
  $page = islandora_paged_content_start_batch_operation($context['results']['pages'], $context);
  $append = FALSE;
  if ($context['sandbox']['progress'] == 0) {
    // Opting to write the consolidated OCR to the filesystem to avoid issues
    // with PHP's memory limit on massive books.
    $context['sandbox']['consolidated_ocr'] = \Drupal::service("file_system")->tempnam("temporary://", "{$paged_content->id}_OCR.txt");
    $append = TRUE;
  }
  $context['results']['successful'][] = $append ? islandora_paged_content_ocr_append($context['sandbox']['consolidated_ocr'], [$page]) : islandora_paged_content_ocr_combine([$page], $context['sandbox']['consolidated_ocr']);
  $finished_message = ['Created OCR with @pages pages.', ['@pages' => count($context['results']['pages'])]];
  if (islandora_paged_content_end_batch_operation($context, 1, $finished_message)) {
    module_load_include('inc', 'islandora_paged_content', 'includes/derivatives');
    islandora_paged_content_update_datastream($paged_content, $context['sandbox']['consolidated_ocr'], 'OCR', 'Consolidated OCR', 'text/plain');
    $context['results']['files'][] = $context['sandbox']['consolidated_ocr'];
    islandora_paged_content_remove_ocr_flag($paged_content);
  }
  $context['message'] = t(
    'Adding page @page of @total to the OCR ...',
    [
      '@page' => $context['sandbox']['progress'],
      '@total' => $context['sandbox']['total'],
    ]
  );
}

/**
 * Gets the batch definition for creating TN, JPG, JP2 datastreams.
 *
 * @param AbstractObject $paged_content
 *   The object the pages belong to.
 * @param array $pages
 *   All the pages to process.
 *
 * @return array
 *   The batch definition.
 */
function islandora_paged_content_create_images_batch(AbstractObject $paged_content, array $pages) {
  return [
    'operations' => [
      [
        'islandora_paged_content_create_images_batch_operation',
        [$paged_content->id, $pages],
      ],
    ],
    'finished' => 'islandora_paged_content_batch_finished',
    'title' => t('Creating Images for @label ...', ['@label' => $paged_content->label]),
    'init_message' => t('Preparing to create images for @label ...', ['@label' => $paged_content->label]),
    'progress_message' => t('Time elapsed: @elapsed <br/>Estimated time remaining @estimate.'),
    'error_message' => t('An error has occurred.'),
    'file' => drupal_get_path('module', 'islandora_paged_content') . '/includes/batch.inc',
  ];
}

/**
 * Batch operation for creating the TN, JPG, JP2 datastreams.
 *
 * @param string $paged_content_id
 *   The paged content the pages belong to.
 * @param array $pages
 *   All the pages to create images for.
 * @param array $context
 *   The context of this batch operation.
 */
function islandora_paged_content_create_images_batch_operation($paged_content_id, array $pages, array &$context) {
  module_load_include('inc', 'islandora_paged_content', 'includes/utilities');
  $paged_content = islandora_object_load($paged_content_id);
  $page = islandora_paged_content_start_batch_operation($pages, $context);
  if ($page) {
    $page = islandora_object_load($page);
    $context['results']['successful'][] = islandora_paged_content_page_derive_image_datastreams($page);
    if (islandora_paged_content_end_batch_operation($context, 1, ['Created images for @successful of @total pages.'])) {
      islandora_paged_content_update_paged_content_thumbnail($paged_content);
    }
    $context['message'] = t(
      'Creating images for page @page of @total ...',
      [
        '@page' => $context['sandbox']['progress'],
        '@total' => $context['sandbox']['total'],
      ]
    );
  }
}

/**
 * Gets the batch definition for re-ordering the given pages.
 *
 * This will order the pages from 1-#pages, the pages will be assigned a
 * sequence number in the order in which they appear.
 *
 * @param AbstractObject $paged_content
 *   The paged_content the pages belong to.
 * @param array $pages
 *   All the pages to sequence.
 *
 * @return array
 *   The batch definition.
 */
function islandora_paged_content_sequence_pages_batch(AbstractObject $paged_content, array $pages) {
  return [
    'operations' => [
      [
        'islandora_paged_content_sequence_pages_batch_operation',
        [$paged_content->id, $pages],
      ],
    ],
    'finished' => 'islandora_paged_content_batch_finished',
    'title' => t("Sequencing @label's pages ...", ['@label' => $paged_content->label]),
    'init_message' => t("Preparing to sequence @label's pages ...", ['@label' => $paged_content->label]),
    'progress_message' => t('Time elapsed: @elapsed <br/>Estimated time remaining @estimate.'),
    'error_message' => t('An error has occurred.'),
    'file' => drupal_get_path('module', 'islandora_paged_content') . '/includes/batch.inc',
  ];
}

/**
 * Changes the ordering of the pages.
 *
 * @param string $paged_content_id
 *   The paged_content the pages belong to.
 * @param array $pages
 *   All the pages in the order they should be in.
 * @param array $context
 *   The context of this batch operation.
 */
function islandora_paged_content_sequence_pages_batch_operation($paged_content_id, array $pages, array &$context) {
  module_load_include('inc', 'islandora_paged_content', 'includes/utilities');
  $paged_content = islandora_object_load($paged_content_id);
  $page = islandora_paged_content_start_batch_operation($pages, $context);
  if ($page) {
    $page = islandora_object_load($page);
    // Change the label of the page to the sequence number if variable is set.
    if (\Drupal::config('islandora_paged_content.settings')->get('islandora_paged_content_page_label')) {
      $page->label = $context['sandbox']['progress'] + 1;
    }

    // Pages are offset from 1 rather than 0.
    islandora_paged_content_set_relationship($page->relationships, ISLANDORA_RELS_EXT_URI, 'isSequenceNumber', $context['sandbox']['progress'] + 1, TRUE);
    // Assume it worked.
    $context['results']['successful'][] = TRUE;
    if (islandora_paged_content_end_batch_operation($context, 1, ['Sequenced @label', ['@label' => $paged_content->label]])) {
      islandora_paged_content_update_paged_content_thumbnail($paged_content);
    }
    $context['message'] = t(
      'Inserting page "@label" (@pid) at position "@pos"',
      [
        '@label' => $page->label,
        '@pid' => $page->id,
        '@pos' => $context['sandbox']['progress'],
      ]
    );
  }
}

/**
 * Gets the batch definition for deleting the given pages.
 *
 * @param AbstractObject $paged_content
 *   The paged content the pages belong to.
 * @param array $pages
 *   All the pages to delete.
 *
 * @return array
 *   The batch definition.
 */
function islandora_paged_content_delete_pages_batch(AbstractObject $paged_content, array $pages) {
  return [
    'operations' => [
      [
        'islandora_paged_content_delete_pages_batch_operation',
        [$paged_content->id, $pages],
      ],
    ],
    'finished' => 'islandora_paged_content_batch_finished',
    'title' => t('Deleting page(s) from @label ...', ['@label' => $paged_content->label]),
    'init_message' => t('Preparing to delete page(s) from @label ...', ['@label' => $paged_content->label]),
    'progress_message' => t('Time elapsed: @elapsed <br/>Estimated time remaining @estimate.'),
    'error_message' => t('An error has occurred.'),
    'file' => drupal_get_path('module', 'islandora_paged_content') . '/includes/batch.inc',
  ];
}

/**
 * Batch operation for deleting a page object.
 *
 * @param string $paged_content_id
 *   The paged_content the pages belong to.
 * @param array $pages
 *   All the pages to delete.
 * @param array $context
 *   The context of this batch operation.
 */
function islandora_paged_content_delete_pages_batch_operation($paged_content_id, array $pages, array &$context) {
  module_load_include('inc', 'islandora_paged_content', 'includes/utilities');
  $paged_content = islandora_object_load($paged_content_id);
  $page = islandora_paged_content_start_batch_operation($pages, $context);
  if ($page) {
    $page = islandora_object_load($page);
    $context['message'] = t('Deleting page "@label" (@pid) ...', ['@label' => $page->label, '@pid' => $page->id]);
    $context['results']['successful'][] = islandora_delete_object($page);
    if (islandora_paged_content_end_batch_operation($context, 1, ['Deleted @successful of @total pages.'])) {
      islandora_paged_content_update_paged_content_thumbnail($paged_content);
    }
  }
}

/**
 * Deletes the parent issue or book object.
 *
 * @param string $pid
 *   The pid of the issue or book object.
 */
function islandora_paged_content_delete_parent_object_operation($pid) {
  $object = islandora_object_load($pid);
  islandora_delete_object($object);
}

/**
 * Gets the batch definition for creating paged content from a PDF.
 *
 * This also updates each page object with the corresponding data.
 *
 * @param AbstractObject $paged_content
 *   The paged_content the pages belong to.
 * @param array $pages
 *   All the pages to include in the PDF.
 * @param array $options
 *   An array of options to pass into the batch operations containing:
 *   -image_format: A string specifying the device to be used when generating
 *   images.
 *   -resolution: A string specifying the DPI of the to be generated image.
 *   -language: A string specifying the language of the OCR text.
 *   -extract_text: An integer specifying whether to extract text from the PDF
 *   page or to let tesseract work its magic for us.
 *   -model: A string specifying the content model of the parent.
 *   -pdf_file: A Drupal file object of the uploaded PDF to be processed.
 *   -parent: A string specifying the parent pid of the to be ingested pages.
 *
 * @return array
 *   The batch definition.
 */
function islandora_paged_content_create_paged_content_from_pdf_batch(AbstractObject $paged_content, array $pages, array $options) {
  return [
    'operations' => [
      [
        'islandora_paged_content_create_page_from_pdf_batch_operation',
        [$pages, $options, $paged_content->id],
      ],
    ],
    'finished' => 'islandora_paged_content_batch_finished',
    'title' => t('Creating pages from @label ...', ['@label' => $paged_content->label]),
    'init_message' => t('Preparing to extract pages from @label ...', ['@label' => $paged_content->label]),
    'progress_message' => t('Time elapsed: @elapsed <br/>Estimated time remaining: @estimate.'),
    'error_message' => t('An error has occurred.'),
    'file' => drupal_get_path('module', 'islandora_paged_content') . '/includes/batch.inc',
  ];
}

/**
 * Batch operation for converting a PDF into a single page.
 *
 * @param array $pages
 *   All the pages to create.
 * @param array $options
 *   An array of options to pass into ghostscript and pdftotext containing:
 *   -image_format: A string specifying the device to be used when generating
 *   images.
 *   -resolution: A string specifying the DPI of the to be generated image.
 *   -language: A string specifying the language of the OCR text.
 *   -extract_text: An integer specifying whether to extract text from the PDF
 *   page or to let tesseract or pdftext work their magic for us.
 *   -model: A string specifying the content model of the parent.
 *   -pdf_file: A Drupal file object of the uploaded PDF to be processed.
 *   -parent: A string specifying the parent pid of the to be ingested pages.
 * @param string $pid
 *   The PID of the parent object.
 * @param array $context
 *   The context of this batch operation.
 */
function islandora_paged_content_create_page_from_pdf_batch_operation(array $pages, array $options, $pid, array &$context) {
  module_load_include('inc', 'islandora_paged_content', 'includes/utilities');
  module_load_include('inc', 'islandora', 'includes/utilities');
  $page = islandora_paged_content_start_batch_operation($pages, $context);
  if ($page) {
    $options['page_number'] = $page;
    $file = $options['pdf_file'];
    $tuque = islandora_get_tuque_connection();
    // Go construct a new object for our page.
    $object = $tuque->repository->constructObject(islandora_get_namespace($pid));
    islandora_paged_content_add_relationships_to_child($object, $options);
    if (\Drupal::config('islandora_paged_content.settings')->get('islandora_paged_content_page_label')) {
      $object->label = $page;
    }
    else {
      $object->label = $file->getFilename() . "-$page";
    }
    $pdf = islandora_paged_content_chop_pdf($file->getFileUri(), $page);
    if ($pdf) {
      $context['results']['successful'][] = islandora_paged_content_update_datastream($object, $pdf, 'PDF', 'PDF', 'application/pdf', 'M');
      $context['results']['files'][] = $pdf;
    }
    else {
      $context['messages'][] = t('Unable to chop the PDF for page @page', ['@page' => $page]);
    }
    $tif = islandora_paged_content_extract_tiff_from_pdf($file->getFileUri(), $page, $options['image_format'], $options['resolution']);
    if ($tif) {
      $context['results']['successful'][] = islandora_paged_content_update_datastream($object, $tif, 'OBJ', 'OBJ Datastream', 'image/tiff', 'M', FALSE);
      $context['results']['files'][] = $tif;
    }
    else {
      $context['messages'][] = t('Unable to extract a TIF for page @page', ['@page' => $page]);
    }
    if ($options['extract_text'] == 'extract') {
      $full_text = islandora_paged_content_extract_text_from_pdf($file->getFileUri(), $page);
      if ($full_text) {
        $context['results']['successful'][] = islandora_paged_content_update_datastream($object, $full_text, 'OCR', 'OCR', 'text/plain', 'M', FALSE);
        $context['results']['files'][] = $full_text;
      }
      else {
        $context['messages'][] = t('Unable to extract a OCR for page @page', ['@page' => $page]);
      }
    }
    $tuque->repository->ingestObject($object);
    islandora_paged_content_end_batch_operation($context);
    $context['message'] = t('Converting page @page of @total from a PDF ...', [
      '@page' => $context['sandbox']['progress'],
      '@total' => $context['sandbox']['total'],
    ]);

  }
}

/**
 * Batch definition for consolidating missing OCR.
 */
function islandora_paged_content_consolidate_missing_ocr_batch() {
  return [
    'title' => t('Batch Consolidate Missing OCR'),
    'init_message' => t('Preparing to consolidate missing OCR ...'),
    'progress_message' => t('Time elapsed: @elapsed <br/>Estimated time remaining: @estimate.'),
    'error_message' => t('An error has occured.'),
    'file' => drupal_get_path('module', 'islandora_paged_content') . '/includes/batch.inc',
    'operations' => [
      ['islandora_paged_content_consolidate_missing_ocr_batch_operation', []],
    ],
  ];
}

/**
 * Batch operation for consolidating missing OCR.
 */
function islandora_paged_content_consolidate_missing_ocr_batch_operation(&$context) {
  module_load_include('inc', 'islandora_paged_content', 'includes/utilities');
  $sandbox = &$context['sandbox'];
  if (empty($sandbox)) {
    $context['finished'] = 0;
    $sandbox['objects_completed'] = 0;
    $sandbox['objects'] = islandora_paged_content_get_missing_ocr_paged_content_objects();
    $sandbox['total'] = count($sandbox['objects']);
  }

  if (!empty($sandbox['objects'])) {
    // If no pages are left to consolidate, append and move on to the next page.
    if (empty($sandbox['current_pages'])) {
      // Skip this step for the first object.
      if (isset($sandbox['current_object'])) {
        $object = islandora_object_load($sandbox['current_object']);
        $datastream = $object->constructDatastream('OCR');
        $datastream->label = 'Consolidated OCR';
        $datastream->mimetype = 'text/plain';
        $datastream->setContentFromFile($sandbox['consolidated_ocr']);
        $object->ingestDatastream($datastream);
        $sandbox['objects_completed']++;
        $context['message'] = "{$sandbox['objects_completed']}/{$sandbox['total']}: Consolidated OCR datastream for {$object->label} ({$object->id}, {$object->models[0]}).";
      }
      if (isset($sandbox['objects'][$sandbox['objects_completed']])) {
        $sandbox['current_object'] = $sandbox['objects'][$sandbox['objects_completed']];
        $sandbox['current_pages'] = islandora_paged_content_get_pages(islandora_object_load($sandbox['current_object']));
        $sandbox['consolidated_ocr'] = \Drupal::service("file_system")->tempnam("temporary://", "{$sandbox['current_object']}_OCR.txt");
      }
    }
    // Otherwise splice off 10 at most, and append them to the consolidated OCR.
    foreach (array_splice($sandbox['current_pages'], 0, min(10, count($sandbox['current_pages']))) as $pid => $page) {
      $page_ocr = islandora_datastream_load('OCR', $pid);
      if (!is_null($page_ocr)) {
        file_put_contents($sandbox['consolidated_ocr'], $page_ocr->content, FILE_APPEND);
      }
      else {
        \Drupal::logger('islandora_paged_content')->warning('Failed to load OCR datastream from @page; skipping.', ['@page' => $pid]);
      }
    }
  }
  else {
    $context['message'] = t("No missing OCR found on paged content objects.");
    $context['finished'] = 1;
    return;
  }
  $context['finished'] = $sandbox['objects_completed'] / $sandbox['total'];
}

/**
 * Gets PIDS of paged content objects with no OCR datastream.
 */
function islandora_paged_content_get_missing_ocr_paged_content_objects() {
  // Base query.
  $query = <<<EOQ
PREFIX islandora-rels-ext: <http://islandora.ca/ontology/relsext#>
PREFIX fedora-view: <info:fedora/fedora-system:def/view#>
PREFIX fedora-model: <info:fedora/fedora-system:def/model#>
SELECT DISTINCT ?pid
FROM <#ri>
WHERE {
  ?page islandora-rels-ext:isPageOf ?pid .
  ?pid fedora-model:hasModel <info:fedora/fedora-system:FedoraObject-3.0> .
  OPTIONAL {
    ?ds fedora-view:disseminationType <info:fedora/*/OCR> .
    ?pid fedora-view:disseminates ?ds .
  }
  FILTER(!BOUND(?ds))
}
EOQ;

  // Make the query.
  $connection = islandora_get_tuque_connection();
  $results = $connection->repository->ri->sparqlQuery($query);
  $return = [];
  foreach ($results as $result) {
    $return[] = $result['pid']['value'];
  }
  return $return;
}
