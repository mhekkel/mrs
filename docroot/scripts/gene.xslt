<!--
	Style sheet for the Gene databank from the NCBI
	
	created: 25 augustus 2006
	
	An xml style sheet to reformat a Gene entry into some HTML.
-->
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:mrs="http://mrs.cmbi.ru.nl/mrs-web/ml" version="1.0">
	
	<xsl:template match="Date">
		<xsl:value-of select="Date_std/Date-std/Date-std_year"/>-<xsl:value-of select="Date_std/Date-std/Date-std_month"/>-<xsl:value-of select="Date_std/Date-std/Date-std_day"/>
	</xsl:template>
	
	<xsl:template match="Gene-commentary">
		<xsl:value-of select="Gene-commentary_label"/>
	</xsl:template>
	
	<xsl:template match="Gene-ref_syn">
		<xsl:for-each select="Gene-ref_syn_E">
			<xsl:if test="position() != 1">, </xsl:if>
			<xsl:value-of select="."/>
		</xsl:for-each>
	</xsl:template>
	
	<xsl:template match="Gene-commentary[Gene-commentary_label='Nomenclature']">
		<xsl:variable name='source' select="Gene-commentary_source/Other-source/Other-source_anchor"/>
		<xsl:for-each select="Gene-commentary_properties/Gene-commentary">
			<tr>
				<td><xsl:value-of select="Gene-commentary_label"/></td>
				<td><span style="position:relative; float:left"><xsl:value-of select="Gene-commentary_text"/></span>
					<span style="position:relative; float:right"><xsl:value-of select="$source"/></span></td>
			</tr>
		</xsl:for-each>
	</xsl:template>
	
	<xsl:template match="PubMedId">
		<xsl:variable name='pmid' select="."/>
		<a href="http://www.ncbi.nlm.nih.gov/entrez/query.fcgi?db=PubMed&amp;cmd=Retrieve&amp;list_uids={$pmid}"><xsl:value-of select="$pmid"/></a>
		<xsl:if test="position() != last()">, </xsl:if>
	</xsl:template>
	
	<xsl:template match="Gene-commentary[Gene-commentary_text]">
		<tr>
			<td><xsl:number value="position()" format="1. "/></td>
			<td><xsl:value-of select="Gene-commentary_text"/></td>
			<td><xsl:apply-templates select="Gene-commentary_refs/Pub/Pub_pmid/PubMedId"/></td>
		</tr>
	</xsl:template>
	
	<xsl:template match="Dbtag">
		<xsl:value-of select="Dbtag_db"/>:<xsl:choose>
				<xsl:when test="Dbtag_tag/Object-id/Object-id_id"><xsl:value-of select="Dbtag_tag/Object-id/Object-id_id"/></xsl:when>
				<xsl:when test="Dbtag_tag/Object-id/Object-id_str"><xsl:value-of select="Dbtag_tag/Object-id/Object-id_str"/></xsl:when>
			</xsl:choose>
	</xsl:template>

	<!-- create a url for a Gene-commentary_source -->	
	<xsl:template match="Other-source" name="make-url">
		
		<xsl:param name="anchor"><xsl:value-of select="Other-source_anchor"/></xsl:param>
		
		<xsl:variable name='dbtag' select="Other-source_src/Dbtag/Dbtag_db"/>
		
		<xsl:choose>
			<xsl:when test="@dbtag='HPRD'">
				<xsl:variable name="id" select="Other-source_src/Dbtag/Dbtag_tag/Object-id/Object-id_str"/>
				<a href="http://www.hprd.org/protein/{$id}">HPRD</a>
			</xsl:when>
			
			<xsl:when test="@dbtag = 'BIND'">
				<xsl:variable name="id" select="Other-source_src/Dbtag/Dbtag_tag/Object-id/Object-id_id"/>
				<a href="http://bind.ca/Action?idsearch={$id}">BIND</a>
			</xsl:when>
			
			<xsl:when test="@dbtag = 'Protein'">
				<xsl:variable name="id" select="Other-source_src/Dbtag/Dbtag_tag/Object-id/Object-id_id"/>
				<mrs:link query='{$anchor}'><xsl:value-of select="$anchor"/></mrs:link>
			</xsl:when>
			
			<xsl:when test="@dbtag = 'Nucleotide'">
				<xsl:choose>
					<xsl:when test="Other-source_src/Dbtag/Dbtag_tag/Object-id/Object-id_str">
						<xsl:variable name="ss-ids" select="Other-source_src/Dbtag/Dbtag_tag/Object-id/Object-id_str"/>
						<mrs:link db='genbank_release' id='{$ss-ids}'><xsl:value-of select="$anchor"/></mrs:link>
					</xsl:when>
					<xsl:otherwise>
						<xsl:variable name="id" select="Other-source_src/Dbtag/Dbtag_tag/Object-id/Object-id_id"/>
						<mrs:link query='{$anchor}'><xsl:value-of select="$anchor"/></mrs:link>
					</xsl:otherwise>
				</xsl:choose>
			</xsl:when>
			
			<xsl:when test="@dbtag = 'GeneID'">
				<xsl:variable name="id" select="Other-source_src/Dbtag/Dbtag_tag/Object-id/Object-id_id"/>
				<mrs:link db='gene' id='{$id}'><xsl:value-of select="$anchor"/></mrs:link>
			</xsl:when>

			<xsl:when test="@dbtag = 'PROT_CDD'">
				<xsl:variable name="id" select="Other-source_src/Dbtag/Dbtag_tag/Object-id/Object-id_id"/>
				<a href="http://www.ncbi.nlm.nih.gov/Structure/cdd/wrpsb.cgi?INPUT_TYPE=precalc&amp;SEQUENCE={$id}"><xsl:value-of select="$anchor"/></a>
			</xsl:when>
			
			<xsl:when test="@dbtag = 'CDD'">
				<xsl:variable name="id" select="Other-source_src/Dbtag/Dbtag_tag/Object-id/Object-id_id"/>
				<a href="http://www.ncbi.nlm.nih.gov/Structure/cdd/cddsrv.cgi?uid={$id}"><xsl:value-of select="$anchor"/></a>
			</xsl:when>
			
			<xsl:when test="@dbtag = 'CCDS'">
				<xsl:variable name="id" select="Other-source_src/Dbtag/Dbtag_tag/Object-id/Object-id_str"/>
				<a href="http://www.ncbi.nlm.nih.gov/CCDS/CcdsBrowse.cgi?REQUEST=CCDS&amp;GO=MainBrowse&amp;DATA={$id}"><xsl:value-of select="$anchor"/></a>
			</xsl:when>

			<xsl:when test="@dbtag = 'UniProt'">
				<xsl:variable name="id" select="Other-source_src/Dbtag/Dbtag_tag/Object-id/Object-id_str"/>
				<mrs:link db='uniprot' index='ac' id='{$id}'><xsl:value-of select="$anchor"/></mrs:link>
			</xsl:when>
			
			<xsl:otherwise>
				<xsl:value-of select="$anchor"/>
			</xsl:otherwise>
		</xsl:choose>

		<xsl:value-of select="Other-source_post-text"/>
	</xsl:template>
	
	<xsl:template name="interaction">
		<tr>
			<td><xsl:value-of select="Gene-commentary_comment/Gene-commentary[position() = 1]/Gene-commentary_source/Other-source/Other-source_anchor"/></td>
			<td>
				<xsl:variable name="other-prot" select="Gene-commentary_comment/Gene-commentary[position() = 2]/Gene-commentary_source/Other-source[position() = 2]/Other-source_anchor"/>
				
				<xsl:choose>
					<xsl:when test="Gene-commentary_comment/Gene-commentary[position() = 2]/Gene-commentary_source/Other-source[position() = 2]/Other-source_src/Dbtag[Dbtag_db='Protein']">
						<xsl:variable name="gi" select="Gene-commentary_comment/Gene-commentary[position() = 2]/Gene-commentary_source/Other-source[position() = 2]/Other-source_src/Dbtag/Dbtag_tag/Object-id/Object-id_id"/>
						<mrs:link db='refseq' index='gi' id='{$gi}'><xsl:value-of select="$other-prot"/></mrs:link>
					</xsl:when>
					
					<xsl:otherwise>
						<xsl:value-of select="$other-prot"/>
					</xsl:otherwise>
				</xsl:choose>

				<xsl:if test="Gene-commentary_text"> (<xsl:value-of select="Gene-commentary_text"/>)</xsl:if>
			</td>
			<td>
				<xsl:variable name="other-gene" select="Gene-commentary_comment/Gene-commentary[position() = 2]/Gene-commentary_source/Other-source[position() = 1]/Other-source_anchor"/>
				
				<xsl:choose>
					<xsl:when test="Gene-commentary_comment/Gene-commentary[position() = 2]/Gene-commentary_source/Other-source[position() = 1]/Other-source_src/Dbtag[Dbtag_db='GeneID']">
						<xsl:variable name="other-id" select="Gene-commentary_comment/Gene-commentary[position() = 2]/Gene-commentary_source/Other-source[position() = 1]/Other-source_src/Dbtag/Dbtag_tag/Object-id/Object-id_id"/>
						<mrs:link db='gene' id='{$other-id}'><xsl:value-of select="$other-gene"/></mrs:link>
					</xsl:when>
					
					<xsl:otherwise>
						<xsl:value-of select="$other-gene"/>
					</xsl:otherwise>
				</xsl:choose>
			</td>
<!--			<td/>-->
			<td>
				<xsl:apply-templates select="Gene-commentary_source"/>
			</td>
			<td><xsl:apply-templates select="Gene-commentary_refs/Pub/Pub_pmid/PubMedId"/></td>
		</tr>
	</xsl:template>
	
	<xsl:template match="Entrezgene">
		<table class="list" cellspacing="0" cellpadding="0" width="100%">
			<caption>
				<b>
				<xsl:choose>
					<xsl:when test="Entrezgene_gene/Gene-ref/Gene-ref_locus"><xsl:value-of select="Entrezgene_gene/Gene-ref/Gene-ref_locus"/></xsl:when>
					<xsl:when test="Entrezgene_gene/Gene-ref/Gene-ref_locus-tag"><xsl:value-of select="Entrezgene_gene/Gene-ref/Gene-ref_locus-tag"/></xsl:when>
				</xsl:choose>
				</b>
				-
				<b><xsl:value-of select="Entrezgene_prot/Prot-ref/Prot-ref_name/Prot-ref_name_E[position() = 1]"/></b>
				
				[ <i><xsl:value-of select="Entrezgene_source/BioSource/BioSource_org/Org-ref/Org-ref_taxname"/></i> ]
				<br/>
				
				<div style="width:100%; display: block;">
					<span style="position:relative; float:left;">
						GeneID: <xsl:value-of select="Entrezgene_track-info/Gene-track/Gene-track_geneid"/>

						<xsl:if test="Entrezgene_gene/Gene-ref/Gene-ref_locus-tag">
							Locus-tag: <xsl:value-of select="Entrezgene_gene/Gene-ref/Gene-ref_locus-tag"/>
						</xsl:if>

						<xsl:if test="Entrezgene_gene/Gene-ref/Gene-ref_db">
							Primary source: <xsl:apply-templates select="Entrezgene_gene/Gene-ref/Gene-ref_db/Dbtag[position() = 1]"/>
						</xsl:if>
							
					</span>
		
					<span style="position:relative; float:right;">
						<xsl:choose>
							<xsl:when test="Entrezgene_track-info/Gene-track/Gene-track_discontinue-date">
								<div style="color:red">
									This gene was discontinued at: <xsl:apply-templates select="Entrezgene_track-info/Gene-track/Gene-track_discontinue-date"/>
								</div>
							</xsl:when>
			
							<xsl:when test="Entrezgene_track-info/Gene-track/Gene-track_update-date">
								Updated: <xsl:apply-templates select="Entrezgene_track-info/Gene-track/Gene-track_update-date"/>
							</xsl:when>
			
							<xsl:when test="Entrezgene_track-info/Gene-track/Gene-track_create-date">
								Created: <xsl:apply-templates select="Entrezgene_track-info/Gene-track/Gene-track_create-date"/>
							</xsl:when>
						</xsl:choose>
					</span>
				</div>
			</caption>

			<tr>
				<th colspan="2">Summary</th>
			</tr>
			
			<xsl:apply-templates select="Entrezgene_properties/Gene-commentary[Gene-commentary_label='Nomenclature']"/>
			
			<xsl:if test="Entrezgene_gene/Gene-ref/Gene-ref_db/Dbtag[position() = 2]">
				<tr>
					<td>See also</td>
					<td>
						<xsl:for-each select="Entrezgene_gene/Gene-ref/Gene-ref_db/Dbtag[position() > 1]">
							<xsl:apply-templates select="."/>
							<xsl:if test="position() != last()">; </xsl:if>
						</xsl:for-each>
					</td>
				</tr>
			</xsl:if>
			
			<tr>
				<td width="20%">Gene type</td>
				<td><xsl:value-of select="Entrezgene_type/@value"/></td>
			</tr>

			<xsl:if test="Entrezgene_track-info/Gene-track/Gene-track_update-date">
				<tr><td>Gene name</td><td><xsl:value-of select="Entrezgene_gene/Gene-ref/Gene-ref_locus"/></td></tr>
			</xsl:if>

			<xsl:if test="Entrezgene_gene/Gene-ref/Gene-ref_desc">
				<tr><td>Gene description</td><td><xsl:value-of select="Entrezgene_gene/Gene-ref/Gene-ref_desc"/></td></tr>
			</xsl:if>
			
			<xsl:if test="Entrezgene_gene/Gene-ref/Gene-ref_syn">
				<tr>
					<td>Gene aliases</td>
					<td><xsl:apply-templates select="Entrezgene_gene/Gene-ref/Gene-ref_syn"/></td>
				</tr>
			</xsl:if>

			<xsl:if test="Entrezgene_comments/Gene-commentary[Gene-commentary_heading='RefSeq Status']">
				<tr>
					<td>Refseq status</td>
					<td><xsl:apply-templates select='Entrezgene_comments/Gene-commentary[Gene-commentary_heading="RefSeq Status"]'/></td>
				</tr>
			</xsl:if>
			
			<tr>
				<td>Organism</td>
				<td>
					<xsl:variable name="tax_id" select="Entrezgene_source/BioSource/BioSource_org/Org-ref/Org-ref_db/Dbtag[Dbtag_db='taxon']/Dbtag_tag/Object-id/Object-id_id"/>
					<mrs:link db='taxonomy' id='{$tax_id}'>
						<xsl:value-of select="Entrezgene_source/BioSource/BioSource_org/Org-ref/Org-ref_taxname"/>
					</mrs:link>
					
					<xsl:for-each select="Entrezgene_source/BioSource/BioSource_org/Org-ref/Org-ref_orgname/OrgName/OrgName_mod">
						(<xsl:value-of select="OrgMod/OrgMod_subtype/@value"/>: <xsl:value-of select="OrgMod/OrgMod_subname"/>)
					</xsl:for-each>
				</td>
			</tr>

			<tr>
				<td>Lineage</td>
				<td><xsl:value-of select="Entrezgene_source/BioSource/BioSource_org/Org-ref/Org-ref_orgname/OrgName/OrgName_lineage"/></td>
			</tr>

			<xsl:if test="Entrezgene_summary">
				<tr>
					<td>Summary</td>
					<td><xsl:value-of select="Entrezgene_summary"/></td>
				</tr>
			</xsl:if>

			<xsl:if test="Entrezgene_source/BioSource/BioSource_subtype/SubSource[SubSource_subtype = '1']">
				<tr>
					<th colspan="2">Genomic context</th>
				</tr>
	
				<tr>
					<td>chromosome</td>
					<td>
						<xsl:value-of select="Entrezgene_source/BioSource/BioSource_subtype/SubSource/SubSource_name"/>
					</td>
				</tr>
			</xsl:if>

			<xsl:choose>
				<xsl:when test="Entrezgene_location/Maps/Maps_display-str">
					<tr><td>location</td><td><xsl:value-of select="Entrezgene_location/Maps/Maps_display-str"/></td></tr>
				</xsl:when>
				<xsl:when test="Entrezgene_gene/Gene-ref/Gene-ref_maploc">
					<tr><td>location</td><td><xsl:value-of select="Entrezgene_gene/Gene-ref/Gene-ref_maploc"/></td></tr>
				</xsl:when>
			</xsl:choose>
			
			<xsl:if test="Entrezgene_comments/Gene-commentary[Gene-commentary_text]">
				<tr>
					<th colspan="2">Bibliography</th>
				</tr>

				<tr>
					<td colspan="2" style="margin:0px;padding:0px">
						<table class="list" cellspacing="0" cellpadding="0" width="100%">
							<tr>
								<th/>
								<th>description</th>
								<th>PubMed</th>
							</tr>
			
							<xsl:apply-templates select="Entrezgene_comments/Gene-commentary[Gene-commentary_text]"/>
						</table>
					</td>
				</tr>
			</xsl:if>
			
			<xsl:if test="Entrezgene_comments/Gene-commentary[Gene-commentary_heading='HIV-1 protein interactions']">
				
				<tr>
					<th colspan="2">HIV-1 protein interactions</th>
				</tr>
				
				<tr>
					<td colspan="2" style="margin:0px;padding:0px">

						<table class="list" cellspacing="0" cellpadding="0" width="100%">
							<tr>
								<th/>
								<th>Protein</th>
								<th>Interaction</th>
								<th>Publication</th>
							</tr>
							
							<xsl:for-each select="Entrezgene_comments/Gene-commentary[Gene-commentary_heading='HIV-1 protein interactions']/Gene-commentary_comment/Gene-commentary">
								<tr>
									<td><xsl:number value="position()" format="1. "/></td>
									<td><xsl:apply-templates select="Gene-commentary_comment/Gene-commentary[position()=1]/Gene-commentary_source/Other-source[position()=1]"/></td>
									<td><xsl:value-of select="Gene-commentary_text"/></td>
									<td><xsl:apply-templates select="Gene-commentary_refs/Pub"/></td>
								</tr>
							</xsl:for-each>
						</table>

					</td>
				</tr>
				
			</xsl:if>

			<xsl:if test="Entrezgene_comments/Gene-commentary[Gene-commentary_heading='Interactions']">
				
				<tr>
					<th colspan="2">Interactions</th>
				</tr>
				
				<tr>
					<td colspan="2" style="margin:0px;padding:0px">

						<table class="list" cellspacing="0" cellpadding="0" width="100%">
							<tr>
								<th>Product</th>
								<th>Interactant</th>
								<th>Other Gene</th>
<!--								<th>Complex</th>-->
								<th>Source</th>
								<th>Publications</th>
							</tr>
							
							<xsl:for-each select="Entrezgene_comments/Gene-commentary[Gene-commentary_heading='Interactions']/Gene-commentary_comment/Gene-commentary">
								<xsl:call-template name="interaction"/>
							</xsl:for-each>
						</table>

					</td>
				</tr>
				
			</xsl:if>

			<xsl:if test="Entrezgene_comments/Gene-commentary[Gene-commentary_heading='Markers (Sequence Tagged Sites/STS)']">
				<tr>
					<th colspan="2">General gene information</th>
				</tr>
	
				<tr>
					<td>Markers</td>
					<td>
						<xsl:for-each select="Entrezgene_comments/Gene-commentary[Gene-commentary_heading='Markers (Sequence Tagged Sites/STS)']/Gene-commentary_comment/Gene-commentary">
							<b><xsl:value-of select="Gene-commentary_source/Other-source/Other-source_anchor"/></b><xsl:value-of select="Gene-commentary_source/Other-source/Other-source_post-text"/>
							
							(Links: <xsl:for-each select="Gene-commentary_source/Other-source/Other-source_src/Dbtag"><xsl:value-of select="Dbtag_db"/>: <xsl:value-of select="Dbtag_tag/Object-id/Object-id_id"/></xsl:for-each>)
							<br/>
							
							<xsl:if test="Gene-commentary_comment/Gene-commentary[Gene-commentary_label='Alternate name']">
								Alternate name: <xsl:for-each select="Gene-commentary_comment/Gene-commentary[Gene-commentary_label='Alternate name']">
								<xsl:value-of select="Gene-commentary_text"/>; 
								</xsl:for-each>
								<br/>
							</xsl:if>
						</xsl:for-each>
					</td>
				</tr>
			</xsl:if>

			<xsl:if test="Entrezgene_properties/Gene-commentary[Gene-commentary_heading='GeneOntology']">
				<tr>
					<td>Gene Ontology</td>
					<td colspan="2" style="margin:0px;padding:0px">
						<table cellpadding="0" cellspacing="0" width="100%">
						<caption>
							Provided by: <xsl:value-of select="Entrezgene_properties/Gene-commentary[Gene-commentary_heading='GeneOntology']/Gene-commentary_source/Other-source//Other-source_anchor"/>
						</caption>

						<tr><th/><th>description</th><th>evidence</th><th>publication</th></tr>
						<xsl:for-each select="Entrezgene_properties/Gene-commentary[Gene-commentary_heading='GeneOntology']/Gene-commentary_comment/Gene-commentary">
							<xsl:variable name="count" select="count(Gene-commentary_comment/Gene-commentary)"/>
							<xsl:variable name="label" select="Gene-commentary_label"/>
							
							<xsl:for-each select="Gene-commentary_comment/Gene-commentary">
								<tr>
									<xsl:if test="position() = 1">
										<td rowspan="{$count}"><b><xsl:value-of select="$label"/></b></td>
									</xsl:if>

									<xsl:variable name="go" select="format-number(Gene-commentary_source/Other-source/Other-source_src/Dbtag/Dbtag_tag/Object-id/Object-id_id, '0000000')"/>

									<td><mrs:link db='go' id='{$go}'><xsl:value-of select="Gene-commentary_source/Other-source/Other-source_anchor"/></mrs:link></td>
									<td><xsl:value-of select="Gene-commentary_source/Other-source/Other-source_post-text"/></td>
									<td><xsl:apply-templates select="Gene-commentary_refs/Pub/Pub_pmid/PubMedId"/></td>

								</tr>
							</xsl:for-each>
						</xsl:for-each>
						</table>
					</td>
				</tr>
			</xsl:if>

			<xsl:if test="Entrezgene_homology">
				<tr>
					<td>Homology</td>
					<td>
						<xsl:for-each select="Entrezgene_homology/Gene-commentary" xml:space="preserve">
							<xsl:value-of select="Gene-commentary_heading"/>
							<xsl:variable name="url" select="Gene-commentary_source/Other-source/Other-source_url"/>
							<a href="{$url}"><xsl:value-of select="Gene-commentary_source/Other-source/Other-source_anchor"/></a>
							<br/>
						</xsl:for-each>
					</td>
				</tr>
			</xsl:if>

			<xsl:if test="Entrezgene_comments/Gene-commentary[Gene-commentary_heading='Phenotypes']">
				<tr>
					<td>Phenotypes</td>
					<td>
						<xsl:for-each select="Entrezgene_comments/Gene-commentary[Gene-commentary_heading='Phenotypes']/Gene-commentary_comment/Gene-commentary" xml:space="preserve">
							<xsl:value-of select="Gene-commentary_text"/>
							<xsl:variable name="mim" select="Gene-commentary_source/Other-source/Other-source_src/Dbtag/Dbtag_tag/Object-id/Object-id_id"/>
							<mrs:link db='omim' id='{$mim}'><xsl:value-of select="Gene-commentary_source/Other-source/Other-source_anchor"/></mrs:link>
							<br/>
						</xsl:for-each>
					</td>
				</tr>
			</xsl:if>

			<xsl:if test="Entrezgene_comments/Gene-commentary[Gene-commentary_heading='Pathways']">
				<tr>
					<td>Pathways</td>
					<td>
						<xsl:for-each select="Entrezgene_comments/Gene-commentary[Gene-commentary_heading='Pathways']/Gene-commentary_comment/Gene-commentary" xml:space="preserve">
							<xsl:value-of select="Gene-commentary_text"/>
							
							<xsl:variable name="pw-url" select="Gene-commentary_source/Other-source/Other-source_url"/>
							<a href="{$pw-url}"><xsl:value-of select="Gene-commentary_source/Other-source/Other-source_anchor"/></a>
							<br/>
						</xsl:for-each>
					</td>
				</tr>
			</xsl:if>

			<tr>
				<th colspan="2">General protein information</th>
			</tr>

			<tr>
				<td>Name</td>
				<td>
					<xsl:for-each select="Entrezgene_prot/Prot-ref/Prot-ref_name/Prot-ref_name_E">
						<xsl:value-of select="."/><br/>
					</xsl:for-each>
				</td>
			</tr>
			
			<xsl:if test="Entrezgene_comments/Gene-commentary[Gene-commentary_heading='NCBI Reference Sequences (RefSeq)']">
				<tr>
					<th colspan="2">NCBI Reference Sequences (RefSeq)</th>
				</tr>
	
				<xsl:for-each select="Entrezgene_comments/Gene-commentary[Gene-commentary_heading='NCBI Reference Sequences (RefSeq)']/Gene-commentary_products/Gene-commentary">
					<tr>
						<td colspan="2">
							<xsl:value-of select="Gene-commentary_heading"/>
							<xsl:apply-templates select="Gene-commentary_source"/>
							<br/>
							
							<div style="position:relative; margin-left:30px">
								<xsl:if test="Gene-commentary_comment/Gene-commentary[Gene-commentary_heading='Source Sequence']">
									Source Sequence <xsl:apply-templates select="Gene-commentary_comment/Gene-commentary[Gene-commentary_heading='Source Sequence']/Gene-commentary_source"/>
									<br/>
								</xsl:if>
								
								<xsl:for-each select="Gene-commentary_products/Gene-commentary">
									Product <xsl:apply-templates select="Gene-commentary_source"/><br/>
									
									<div style="position:relative; margin-left:30px">
										
										<xsl:for-each select="Gene-commentary_comment/Gene-commentary">
											<xsl:value-of select="Gene-commentary_heading"/>
											<xsl:apply-templates select="Gene-commentary_source"/>
											<br/>
											
											<div style="position:relative; margin-left:30px">
												<xsl:for-each select="Gene-commentary_comment/Gene-commentary">
													<xsl:apply-templates select="Gene-commentary_source"/>
													<br/>

													<div style="position:relative; margin-left:30px">
														<xsl:for-each select="Gene-commentary_comment/Gene-commentary">
															<xsl:value-of select="Gene-commentary_text"/>
															<br/>
														</xsl:for-each>
													</div>

												</xsl:for-each>
											</div>
										</xsl:for-each>
									</div>
								</xsl:for-each>
							</div>
						</td>
					</tr>
				</xsl:for-each>
			</xsl:if>

			<xsl:if test="Entrezgene_comments/Gene-commentary[Gene-commentary_heading='Related Sequences']">
				<tr>
					<th colspan="2">Related Sequences</th>
				</tr>

				<tr>
					<td colspan="2" style="margin:0px;padding:0px">
						<table cellspacing="0" cellpadding="0">
							<tr><th/><th>Nucleotide</th><th>Protein</th></tr>

							<xsl:for-each select="Entrezgene_comments/Gene-commentary[Gene-commentary_heading='Related Sequences']/Gene-commentary_products/Gene-commentary[Gene-commentary_heading]">
								<tr>
									<td><xsl:value-of select="Gene-commentary_heading"/></td>
									<td><xsl:apply-templates select="Gene-commentary_source"/></td>
									<td><xsl:apply-templates select="Gene-commentary_products/Gene-commentary/Gene-commentary_source"/></td>
								</tr>
							</xsl:for-each>
						</table>
					</td>
				</tr>
				
				<xsl:if test="Entrezgene_comments/Gene-commentary[Gene-commentary_heading='Related Sequences']/Gene-commentary_products/Gene-commentary[Gene-commentary_text='None']">
					<tr>
						<td colspan="2" style="margin:0px;padding:0px">
							<table cellspacing="0" cellpadding="0">
								<caption colspan="2">Protein accession links</caption>
								<xsl:for-each select="Entrezgene_comments/Gene-commentary[Gene-commentary_heading='Related Sequences']/Gene-commentary_products/Gene-commentary[Gene-commentary_text='None']/Gene-commentary_products/Gene-commentary">
									<tr>
										<td><xsl:value-of select="Gene-commentary_accession"/></td>
										<xsl:for-each select="Gene-commentary_source/Other-source">
											<td>
												<xsl:call-template name="make-url">
													<xsl:with-param name="anchor">
														<xsl:choose>
															<xsl:when test="position() = 1">GenPept</xsl:when>
															<xsl:when test="position() = 2"><xsl:value-of select="Other-source_src/Dbtag/Dbtag_db"/></xsl:when>
														</xsl:choose>
													</xsl:with-param>
												</xsl:call-template>
											</td>
										</xsl:for-each>
									</tr>
								</xsl:for-each>
							</table>
						</td>
					</tr>
				</xsl:if>
			</xsl:if>
			
		</table>
	</xsl:template>	
	
	<xsl:template match="/">
		<xsl:apply-templates select="Entrezgene"/>
	</xsl:template>

</xsl:stylesheet>
